// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_condition_timing.hpp"

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

fsim::runtime::simir::ModulePathExpression terminal_expression(
    const fsim::runtime::simir::SignalId signal)
{
    using namespace fsim::runtime::simir;
    ModulePathExpression result;
    ModulePathExpressionNode node;
    node.operation = ModulePathExpressionOperator::terminal;
    node.terminal = { signal, 0U, 1U };
    node.width = 1U;
    result.nodes.push_back(std::move(node));
    return result;
}

fsim::runtime::simir::ModulePathExpression compound_expression(
    const fsim::runtime::simir::SignalId left,
    const fsim::runtime::simir::SignalId right)
{
    using namespace fsim::runtime::simir;
    auto result = terminal_expression(left);
    auto rhs = terminal_expression(right).nodes.front();
    result.nodes.push_back(std::move(rhs));
    ModulePathExpressionNode operation;
    operation.operation = ModulePathExpressionOperator::logical_binary;
    operation.operands = { 0U, 1U };
    operation.logical = LogicalBinaryOperator::logical_and;
    operation.width = 1U;
    result.nodes.push_back(std::move(operation));
    result.root = 2U;
    return result;
}

fsim::frontend::SourceSpan source_span()
{
    fsim::frontend::SourceSpan result;
    result.source_name = "condition.sdf";
    result.begin = { 8U, 4U, 3U };
    result.end = { 24U, 4U, 19U };
    return result;
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    for (const auto name : { "top.u.ref", "top.u.data", "top.u.enable",
             "top.u.mode", "top.u.notifier" }) {
        const auto id
            = static_cast<runtime::simir::SignalId>(state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 1U;
        info.type_name = "logic";
        info.source_domain = frontend::ValueDomain::Logic4;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(
            name, runtime::PackedLogic4::from_msb_string("0"));
        state.signal_names.emplace_back(name, id);
    }
    state.signals[3].initial_value
        = runtime::PackedLogic4::from_msb_string("1");

    runtime::simir::ModuleTimingCheck setuphold;
    setuphold.id = 0U;
    setuphold.identity = "sdf:timingcheck:top.u:condition:0";
    setuphold.kind = runtime::simir::ModuleTimingCheckKind::setuphold;
    setuphold.reference.terminal = { 0U, 0U, 1U };
    setuphold.reference.edge = runtime::simir::ModulePathEdge::posedge;
    setuphold.reference.condition = compound_expression(2U, 3U);
    setuphold.data = runtime::simir::ModuleTimingEvent { };
    setuphold.data->terminal = { 1U, 0U, 1U };
    setuphold.data->edge_descriptors = { "01", "0x", "x1" };
    setuphold.data->condition = terminal_expression(2U);
    setuphold.limits = { -2, 5 };
    setuphold.notifier = 4U;
    setuphold.timestamp_condition = terminal_expression(2U);
    setuphold.timecheck_condition = compound_expression(2U, 3U);
    state.verilog_timing_checks.push_back(std::move(setuphold));

    runtime::simir::ModuleTimingCheck nochange;
    nochange.id = 1U;
    nochange.identity = "sdf:timingcheck:top.u:condition:1";
    nochange.kind = runtime::simir::ModuleTimingCheckKind::nochange;
    nochange.reference.terminal = { 0U, 0U, 1U };
    nochange.reference.edge = runtime::simir::ModulePathEdge::posedge;
    nochange.reference.condition = terminal_expression(2U);
    nochange.data = runtime::simir::ModuleTimingEvent { };
    nochange.data->terminal = { 1U, 0U, 1U };
    nochange.data->edge = runtime::simir::ModulePathEdge::posedge;
    nochange.limits = { -3, 4 };
    state.verilog_timing_checks.push_back(std::move(nochange));

    runtime::simir::ModuleTimingCheck setup;
    setup.id = 2U;
    setup.identity = "sdf:timingcheck:top.u:condition:2";
    setup.kind = runtime::simir::ModuleTimingCheckKind::setup;
    setup.reference.terminal = { 0U, 0U, 1U };
    setup.data = runtime::simir::ModuleTimingEvent { };
    setup.data->terminal = { 1U, 0U, 1U };
    setup.limits = { 1 };
    state.verilog_timing_checks.push_back(std::move(setup));

    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "condition timing fixture must be valid");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfAnnotationSummary> make_summary(
    const std::size_t count)
{
    using namespace fsim;
    auto ir = std::make_shared<const frontend::SdfIr>(
        frontend::SdfRevision::Sdf40, frontend::SdfRevisionAdapter::None,
        "condition.sdf", std::nullopt, std::vector<frontend::SdfIrCell> { },
        std::vector<frontend::SdfIrNode> { }, "condition-ir");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir,
        "condition.sdf", "condition-source", std::nullopt, '.', "project",
        "design", std::vector<app::SdfAnnotationRoot> { }, "condition-scope");
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { }, "condition-cells");
    auto endpoints = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::vector<app::SdfResolvedNodeEndpoints> { }, "condition-endpoints");
    return std::make_shared<const app::SdfAnnotationSummary>(endpoints,
        std::vector<app::SdfAnnotationTargetSummary> { }, count, count, 0U, 0U,
        0U, "condition-summary");
}

fsim::app::SdfResolvedEndpoint endpoint(
    const fsim::app::SdfEndpointRole role,
    const fsim::runtime::simir::SignalId signal,
    std::string edge_identity = { })
{
    fsim::app::SdfResolvedEndpoint result;
    result.role = role;
    result.object_kind = fsim::app::SdfEndpointObjectKind::HdlNet;
    result.instance_path = "top.u";
    result.object_path = "top.u.signal-" + std::to_string(signal);
    result.signal = signal;
    result.object_width = 1U;
    result.language = fsim::app::SdfScopeRootLanguage::SystemVerilog;
    result.edge_identity = std::move(edge_identity);
    return result;
}

std::vector<fsim::app::SdfPlannedAnnotation> valid_annotations()
{
    using namespace fsim;
    std::vector<app::SdfPlannedAnnotation> result;

    app::SdfPlannedAnnotation setuphold;
    setuphold.node_id = 1U;
    setuphold.construct_kind = frontend::SdfConstructKind::SetupHold;
    setuphold.target_kind = app::SdfTimingTargetKind::TimingCheck;
    setuphold.target_identity = "sdf:timingcheck:top.u:condition:0";
    setuphold.condition_identity = "sdf-condition-enable-and-mode";
    setuphold.edge_identities = { "sdf-data-edge", "sdf-reference-edge" };
    setuphold.endpoints = {
        endpoint(app::SdfEndpointRole::TimingReference, 0U,
            "sdf-reference-edge"),
        endpoint(app::SdfEndpointRole::TimingData, 1U, "sdf-data-edge"),
        endpoint(app::SdfEndpointRole::Condition, 2U),
        endpoint(app::SdfEndpointRole::Condition, 3U),
    };
    setuphold.before_check_ticks = { -2, 5 };
    setuphold.after_check_ticks = { -3, 7 };
    setuphold.source = source_span();
    setuphold.canonical_identity = "condition-annotation-0";
    result.push_back(std::move(setuphold));

    app::SdfPlannedAnnotation nochange;
    nochange.node_id = 2U;
    nochange.construct_kind = frontend::SdfConstructKind::NoChange;
    nochange.target_kind = app::SdfTimingTargetKind::TimingCheck;
    nochange.target_identity = "sdf:timingcheck:top.u:condition:1";
    nochange.endpoints = {
        endpoint(app::SdfEndpointRole::TimingReference, 0U),
        endpoint(app::SdfEndpointRole::TimingData, 1U),
    };
    nochange.before_check_ticks = { -3, 4 };
    nochange.after_check_ticks = { -4, 6 };
    nochange.source = source_span();
    nochange.canonical_identity = "condition-annotation-1";
    result.push_back(std::move(nochange));

    app::SdfPlannedAnnotation setup;
    setup.node_id = 3U;
    setup.construct_kind = frontend::SdfConstructKind::Setup;
    setup.target_kind = app::SdfTimingTargetKind::TimingCheck;
    setup.target_identity = "sdf:timingcheck:top.u:condition:2";
    setup.endpoints = {
        endpoint(app::SdfEndpointRole::TimingReference, 0U),
        endpoint(app::SdfEndpointRole::TimingData, 1U),
    };
    setup.before_check_ticks = { 1 };
    setup.after_check_ticks = { 2 };
    setup.source = source_span();
    setup.canonical_identity = "condition-annotation-2";
    result.push_back(std::move(setup));
    return result;
}

std::shared_ptr<const fsim::app::SdfAnnotationPlan> make_plan(
    std::vector<fsim::app::SdfPlannedAnnotation> annotations)
{
    return std::make_shared<const fsim::app::SdfAnnotationPlan>(
        make_summary(annotations.size()), fsim::app::SdfValuePolicy { },
        std::move(annotations), "condition-plan");
}

void require_code(const fsim::app::SdfConditionTimingResult& result,
    const std::string_view code)
{
    require(std::ranges::find(result.diagnostics, code,
                &fsim::frontend::Diagnostic::code)
            != result.diagnostics.end(),
        "missing conditional timing diagnostic");
}

void test_condition_edge_notifier_and_signed_binding()
{
    const auto design = make_design();
    const auto before = design.state();
    const auto result = fsim::app::apply_sdf_condition_timing(
        make_plan(valid_annotations()), design);
    require(result.ok() && result.application->checks().size() == 3U,
        "all conditional timing overlays must publish atomically");
    const auto* matched = result.application->find_check(0U);
    require(matched
            && matched->condition_disposition
                == fsim::app::SdfTimingConditionDisposition::SdfMatched
            && matched->condition_signals
                == std::vector<fsim::runtime::simir::SignalId>({ 2U, 3U })
            && matched->notifier == 4U && matched->edge_qualified
            && matched->negative_limits && matched->stable_order == 0U
            && matched->effective_check.limits
                == std::vector<std::int64_t>({ -3, 7 })
            && matched->effective_check.data->edge_descriptors
                == std::vector<std::string>({ "01", "0x", "x1" })
            && !matched->condition_program_identity.empty(),
        "application must retain exact condition, edge, notifier, signed-window, and stable-order state");
    const auto* elaborated_only = result.application->find_check(1U);
    const auto* unconditional = result.application->find_check(2U);
    require(elaborated_only
            && elaborated_only->condition_disposition
                == fsim::app::SdfTimingConditionDisposition::ElaboratedOnly
            && unconditional
            && unconditional->condition_disposition
                == fsim::app::SdfTimingConditionDisposition::Unconditional,
        "source-only and unconditional applicability must remain observable");
    const auto after = design.state();
    require(before.verilog_timing_checks[0].limits
                == after.verilog_timing_checks[0].limits
            && before.verilog_timing_checks[0].reference.condition.nodes.size()
                == after.verilog_timing_checks[0].reference.condition.nodes.size()
            && before.verilog_timing_checks[0].notifier
                == after.verilog_timing_checks[0].notifier,
        "conditional timing application must not mutate elaboration");
}

std::pair<std::size_t, std::string> run_effective_check(
    const fsim::runtime::simir::ModuleTimingCheck& source,
    const std::string_view initial_enable,
    const std::optional<fsim::runtime::StableOrder> enable_order,
    const fsim::runtime::StableOrder reference_order)
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    Interpreter interpreter;
    const auto reference = interpreter.add_signal(
        { "reference", PackedLogic4::from_msb_string("0") });
    const auto data = interpreter.add_signal(
        { "data", PackedLogic4::from_msb_string("0") });
    const auto enable = interpreter.add_signal(
        { "enable", PackedLogic4::from_msb_string(initial_enable) });
    const auto mode = interpreter.add_signal(
        { "mode", PackedLogic4::from_msb_string("1") });
    const auto notifier = interpreter.add_signal(
        { "notifier", PackedLogic4::from_msb_string("0") });
    require(reference == 0U && data == 1U && enable == 2U && mode == 3U
            && notifier == 4U,
        "runtime condition fixture must preserve elaborated signal IDs");
    static_cast<void>(interpreter.add_module_timing_check(source));
    std::size_t reports { };
    interpreter.set_report_hook([&](const ProcessId, const std::string_view,
                                    const AssertionSeverity,
                                    const SourceLocation&, const SimulationTick,
                                    const std::uint64_t) { ++reports; });
    if (enable_order) {
        interpreter.schedule_signal_at(enable,
            PackedLogic4::from_msb_string("1"), 1U, *enable_order);
    }
    interpreter.schedule_signal_at(reference,
        PackedLogic4::from_msb_string("1"), 1U, reference_order);
    interpreter.schedule_signal_at(
        data, PackedLogic4::from_msb_string("1"), 5U, 0U);
    const auto run = interpreter.run();
    require(run.status == RunStatus::completed,
        "effective conditional timing check must complete");
    return { reports, interpreter.signal_value(notifier).to_msb_string() };
}

void test_four_state_and_same_tick_runtime_behavior()
{
    const auto design = make_design();
    const auto applied = fsim::app::apply_sdf_condition_timing(
        make_plan(valid_annotations()), design);
    require(applied.ok(), "runtime evidence requires a valid timing overlay");
    const auto& check = applied.application->find_check(0U)->effective_check;
    require(run_effective_check(check, "x", std::nullopt, 0U)
            == std::pair<std::size_t, std::string>({ 0U, "0" }),
        "X conditions must remain disabled without widening applicability");
    require(run_effective_check(check, "z", std::nullopt, 0U)
            == std::pair<std::size_t, std::string>({ 0U, "0" }),
        "Z conditions must remain disabled without widening applicability");
    require(run_effective_check(check, "1", std::nullopt, 0U)
            == std::pair<std::size_t, std::string>({ 1U, "1" }),
        "a true compound condition must enable the signed check and notifier");
    const auto enable_first
        = run_effective_check(check, "0", 0U, 1U);
    const auto enable_first_repeat
        = run_effective_check(check, "0", 0U, 1U);
    const auto reference_first
        = run_effective_check(check, "0", 2U, 1U);
    const auto reference_first_repeat
        = run_effective_check(check, "0", 2U, 1U);
    require(enable_first == enable_first_repeat
            && reference_first == reference_first_repeat,
        "same-tick condition/reference scheduling must be deterministic for each stable order");
}

void test_mismatch_and_resource_rejection()
{
    const auto design = make_design();
    auto annotations = valid_annotations();
    annotations[0].endpoints.pop_back();
    auto result = fsim::app::apply_sdf_condition_timing(
        make_plan(std::move(annotations)), design);
    require(!result.ok() && !result.application,
        "a partial compound condition binding must reject atomically");
    require_code(result, "FSIM-SDF-CONDITION-CHECK-003");

    annotations = valid_annotations();
    annotations[2].endpoints[0].edge_identity = "stale-edge";
    annotations[2].edge_identities = { "stale-edge" };
    result = fsim::app::apply_sdf_condition_timing(
        make_plan(std::move(annotations)), design);
    require(!result.ok(), "an unmatched edge qualifier must reject");
    require_code(result, "FSIM-SDF-CONDITION-CHECK-002");

    annotations = valid_annotations();
    annotations[2].after_check_ticks = { -1 };
    result = fsim::app::apply_sdf_condition_timing(
        make_plan(std::move(annotations)), design);
    require(!result.ok(), "a negative simple timing check must reject");
    require_code(result, "FSIM-SDF-CONDITION-CHECK-004");

    auto limits = fsim::app::SdfConditionTimingLimits { };
    limits.max_expression_nodes_per_check = 1U;
    result = fsim::app::apply_sdf_condition_timing(
        make_plan(valid_annotations()), design, limits);
    require(!result.ok() && !result.application,
        "condition-program resource failure must reject atomically");
    require_code(result, "FSIM-SDF-CONDITION-CHECK-004");
}
} // namespace

int main()
{
    try {
        test_condition_edge_notifier_and_signed_binding();
        test_four_state_and_same_tick_runtime_behavior();
        test_mismatch_and_resource_rejection();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}

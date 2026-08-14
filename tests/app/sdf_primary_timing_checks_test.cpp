// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_primary_timing_checks.hpp"

#include <iostream>
#include <limits>
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
    result.source_name = "primary.sdf";
    result.begin = { 6U, 5U, 2U };
    result.end = { 18U, 5U, 14U };
    return result;
}

struct KindPair {
    fsim::frontend::SdfConstructKind construct;
    fsim::runtime::simir::ModuleTimingCheckKind runtime;
};

constexpr KindPair primary_kinds[] = {
    { fsim::frontend::SdfConstructKind::Setup,
        fsim::runtime::simir::ModuleTimingCheckKind::setup },
    { fsim::frontend::SdfConstructKind::Hold,
        fsim::runtime::simir::ModuleTimingCheckKind::hold },
    { fsim::frontend::SdfConstructKind::SetupHold,
        fsim::runtime::simir::ModuleTimingCheckKind::setuphold },
    { fsim::frontend::SdfConstructKind::Recovery,
        fsim::runtime::simir::ModuleTimingCheckKind::recovery },
    { fsim::frontend::SdfConstructKind::Removal,
        fsim::runtime::simir::ModuleTimingCheckKind::removal },
    { fsim::frontend::SdfConstructKind::RecRem,
        fsim::runtime::simir::ModuleTimingCheckKind::recrem },
};

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    for (const auto name : { "top.u.ref", "top.u.data", "top.u.dref",
             "top.u.ddata" }) {
        const auto id
            = static_cast<runtime::simir::SignalId>(state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 1U;
        info.type_name = "logic";
        info.source_domain = frontend::ValueDomain::Logic4;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(name, runtime::PackedLogic4(1U));
        state.signal_names.emplace_back(name, id);
    }
    for (std::size_t index = 0; index < std::size(primary_kinds); ++index) {
        runtime::simir::ModuleTimingCheck check;
        check.id = static_cast<std::uint32_t>(index);
        check.identity = "sdf:timingcheck:top.u:" + std::to_string(index);
        check.kind = primary_kinds[index].runtime;
        check.reference.terminal = { 0U, 0U, 1U };
        check.data = runtime::simir::ModuleTimingEvent { };
        check.data->terminal = { 1U, 0U, 1U };
        const bool combined
            = check.kind == runtime::simir::ModuleTimingCheckKind::setuphold
            || check.kind == runtime::simir::ModuleTimingCheckKind::recrem;
        check.limits = combined ? std::vector<std::int64_t> { -10, 20 }
                                : std::vector<std::int64_t> { 10 };
        if (combined) {
            check.delayed_reference = { 2U, 0U, 1U };
            check.delayed_data = { 3U, 0U, 1U };
        }
        state.verilog_timing_checks.push_back(std::move(check));
    }
    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "primary timing-check fixture must be valid");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfAnnotationSummary> make_summary(
    const std::size_t count)
{
    using namespace fsim;
    auto ir = std::make_shared<const frontend::SdfIr>(
        frontend::SdfRevision::Sdf40, frontend::SdfRevisionAdapter::None,
        "primary.sdf", std::nullopt, std::vector<frontend::SdfIrCell> { },
        std::vector<frontend::SdfIrNode> { }, "primary-ir-identity");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir,
        "primary.sdf", "primary-source-identity", std::nullopt, '.', "project",
        "design", std::vector<app::SdfAnnotationRoot> { },
        "primary-scope-identity");
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { }, "primary-cells-identity");
    auto endpoints = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::vector<app::SdfResolvedNodeEndpoints> { },
        "primary-endpoint-identity");
    return std::make_shared<const app::SdfAnnotationSummary>(endpoints,
        std::vector<app::SdfAnnotationTargetSummary> { }, count, count, 0U, 0U,
        0U, "primary-summary-identity");
}

fsim::app::SdfResolvedEndpoint endpoint(
    const fsim::app::SdfEndpointRole role,
    const fsim::runtime::simir::SignalId signal, std::string path)
{
    fsim::app::SdfResolvedEndpoint result;
    result.role = role;
    result.object_kind = fsim::app::SdfEndpointObjectKind::HdlNet;
    result.instance_path = "top.u";
    result.object_path = std::move(path);
    result.signal = signal;
    result.object_width = 1U;
    result.language = fsim::app::SdfScopeRootLanguage::SystemVerilog;
    return result;
}

std::vector<fsim::app::SdfPlannedAnnotation> valid_annotations()
{
    std::vector<fsim::app::SdfPlannedAnnotation> result;
    for (std::size_t index = 0; index < std::size(primary_kinds); ++index) {
        fsim::app::SdfPlannedAnnotation annotation;
        annotation.node_id = index + 1U;
        annotation.cell_id = 1U;
        annotation.construct_kind = primary_kinds[index].construct;
        annotation.target_kind = fsim::app::SdfTimingTargetKind::TimingCheck;
        annotation.target_instance_path = "top.u";
        annotation.target_identity
            = "sdf:timingcheck:top.u:" + std::to_string(index);
        annotation.endpoints = {
            endpoint(fsim::app::SdfEndpointRole::TimingReference, 0U,
                "top.u.ref"),
            endpoint(fsim::app::SdfEndpointRole::TimingData, 1U, "top.u.data"),
        };
        const bool combined
            = primary_kinds[index].runtime
                == fsim::runtime::simir::ModuleTimingCheckKind::setuphold
            || primary_kinds[index].runtime
                == fsim::runtime::simir::ModuleTimingCheckKind::recrem;
        annotation.before_check_ticks
            = combined ? std::vector<std::int64_t> { -10, 20 }
                       : std::vector<std::int64_t> { 10 };
        annotation.after_check_ticks
            = combined ? std::vector<std::int64_t> { -5, 40 }
                       : std::vector<std::int64_t> { 30 };
        annotation.source = source_span();
        annotation.source_identity = "primary-source";
        annotation.canonical_identity
            = "primary-annotation:" + std::to_string(index);
        result.push_back(std::move(annotation));
    }
    return result;
}

std::shared_ptr<const fsim::app::SdfAnnotationPlan> make_plan(
    std::vector<fsim::app::SdfPlannedAnnotation> annotations)
{
    return std::make_shared<const fsim::app::SdfAnnotationPlan>(
        make_summary(annotations.size()), fsim::app::SdfValuePolicy { },
        std::move(annotations), "primary-plan-identity");
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfPrimaryTimingCheckResult& result,
    const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error(
            "missing primary-check diagnostic " + std::string { code });
    return *found;
}

void test_all_primary_checks_and_combined_limits()
{
    const auto design = make_design();
    const auto before = design.state();
    const auto result = fsim::app::apply_sdf_primary_timing_checks(
        make_plan(valid_annotations()), design);
    require(result.ok() && result.application->checks().size() == 6U,
        "all six primary timing-check kinds must publish atomically");
    const auto* setuphold = result.application->find_check(2U);
    require(setuphold != nullptr
            && setuphold->effective_limits
                == std::vector<std::int64_t>({ -5, 40 })
            && setuphold->effective_check.delayed_reference
                == fsim::runtime::simir::ModulePathTerminal({ 2U, 0U, 1U })
            && setuphold->effective_check.delayed_data
                == fsim::runtime::simir::ModulePathTerminal({ 3U, 0U, 1U }),
        "combined checks must retain distinct limits and delayed terminals");
    const auto after = design.state();
    require(before.verilog_timing_checks[2].limits
                == after.verilog_timing_checks[2].limits
            && before.verilog_timing_checks[2].delayed_reference
                == after.verilog_timing_checks[2].delayed_reference,
        "primary timing-check application must not mutate elaboration");
}

void test_stale_kind_event_and_shape_rejection()
{
    const auto design = make_design();
    auto annotations = valid_annotations();
    annotations[0].before_check_ticks = { 9 };
    auto result = fsim::app::apply_sdf_primary_timing_checks(
        make_plan(std::move(annotations)), design);
    require(!result.ok(), "stale current limits must reject");
    require_diagnostic(result, "FSIM-SDF-PRIMARY-CHECK-003");

    annotations = valid_annotations();
    annotations[1].construct_kind = fsim::frontend::SdfConstructKind::Setup;
    result = fsim::app::apply_sdf_primary_timing_checks(
        make_plan(std::move(annotations)), design);
    require(!result.ok(), "kind mismatch must reject");
    require_diagnostic(result, "FSIM-SDF-PRIMARY-CHECK-001");

    annotations = valid_annotations();
    annotations[3].endpoints[1].signal = 0U;
    result = fsim::app::apply_sdf_primary_timing_checks(
        make_plan(std::move(annotations)), design);
    require(!result.ok(), "data/reference role mismatch must reject");
    require_diagnostic(result, "FSIM-SDF-PRIMARY-CHECK-002");

    annotations = valid_annotations();
    annotations[2].after_check_ticks = { 30 };
    result = fsim::app::apply_sdf_primary_timing_checks(
        make_plan(std::move(annotations)), design);
    require(!result.ok(), "combined-check partial limits must reject");
    require_diagnostic(result, "FSIM-SDF-PRIMARY-CHECK-003");
}

void test_overflow_and_resource_rejection()
{
    const auto design = make_design();
    auto annotations = valid_annotations();
    annotations[2].after_check_ticks = { -30, 20 };
    auto result = fsim::app::apply_sdf_primary_timing_checks(
        make_plan(std::move(annotations)), design);
    require(!result.ok(), "nonpositive combined signed window must reject");
    require_diagnostic(result, "FSIM-SDF-PRIMARY-CHECK-003");

    auto limits = fsim::app::SdfPrimaryTimingCheckLimits { };
    limits.max_checks = 5U;
    result = fsim::app::apply_sdf_primary_timing_checks(
        make_plan(valid_annotations()), design, limits);
    require(!result.ok() && !result.application,
        "check-count resource limit must reject atomically");
    require_diagnostic(result, "FSIM-SDF-PRIMARY-CHECK-004");
}
} // namespace

int main()
{
    try {
        test_all_primary_checks_and_combined_limits();
        test_stale_kind_event_and_shape_rejection();
        test_overflow_and_resource_rejection();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}

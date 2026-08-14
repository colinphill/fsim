// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_secondary_timing_checks.hpp"

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

struct KindPair {
    fsim::frontend::SdfConstructKind construct;
    fsim::runtime::simir::ModuleTimingCheckKind runtime;
};

constexpr KindPair kinds[] = {
    { fsim::frontend::SdfConstructKind::Skew,
        fsim::runtime::simir::ModuleTimingCheckKind::skew },
    { fsim::frontend::SdfConstructKind::Skew,
        fsim::runtime::simir::ModuleTimingCheckKind::timeskew },
    { fsim::frontend::SdfConstructKind::BidirectSkew,
        fsim::runtime::simir::ModuleTimingCheckKind::fullskew },
    { fsim::frontend::SdfConstructKind::Width,
        fsim::runtime::simir::ModuleTimingCheckKind::width },
    { fsim::frontend::SdfConstructKind::Period,
        fsim::runtime::simir::ModuleTimingCheckKind::period },
    { fsim::frontend::SdfConstructKind::NoChange,
        fsim::runtime::simir::ModuleTimingCheckKind::nochange },
};

fsim::frontend::SourceSpan span()
{
    fsim::frontend::SourceSpan result;
    result.source_name = "secondary.sdf";
    result.begin = { 5U, 4U, 2U };
    result.end = { 15U, 4U, 12U };
    return result;
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    for (const auto name : { "top.u.ref", "top.u.data" }) {
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
    for (std::size_t index = 0; index < std::size(kinds); ++index) {
        runtime::simir::ModuleTimingCheck check;
        check.id = static_cast<std::uint32_t>(index);
        check.identity = "sdf:timingcheck:top.u:secondary:"
            + std::to_string(index);
        check.kind = kinds[index].runtime;
        check.reference.terminal = { 0U, 0U, 1U };
        check.reference.edge = runtime::simir::ModulePathEdge::posedge;
        if (check.kind != runtime::simir::ModuleTimingCheckKind::width
            && check.kind != runtime::simir::ModuleTimingCheckKind::period) {
            check.data = runtime::simir::ModuleTimingEvent { };
            check.data->terminal = { 1U, 0U, 1U };
        }
        const bool compound
            = check.kind == runtime::simir::ModuleTimingCheckKind::fullskew
            || check.kind == runtime::simir::ModuleTimingCheckKind::nochange;
        check.limits = check.kind
                == runtime::simir::ModuleTimingCheckKind::nochange
            ? std::vector<std::int64_t> { -5, 10 }
            : compound ? std::vector<std::int64_t> { 5, 10 }
                       : std::vector<std::int64_t> { 5 };
        if (check.kind == runtime::simir::ModuleTimingCheckKind::width)
            check.threshold = 2U;
        check.event_based = true;
        check.remain_active = true;
        state.verilog_timing_checks.push_back(std::move(check));
    }
    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "secondary timing-check fixture must be valid");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfAnnotationSummary> summary(
    const std::size_t count)
{
    using namespace fsim;
    auto ir = std::make_shared<const frontend::SdfIr>(
        frontend::SdfRevision::Sdf40, frontend::SdfRevisionAdapter::None,
        "secondary.sdf", std::nullopt, std::vector<frontend::SdfIrCell> { },
        std::vector<frontend::SdfIrNode> { }, "secondary-ir");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir,
        "secondary.sdf", "secondary-source", std::nullopt, '.', "project",
        "design", std::vector<app::SdfAnnotationRoot> { }, "secondary-scope");
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { }, "secondary-cells");
    auto endpoints = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::vector<app::SdfResolvedNodeEndpoints> { }, "secondary-endpoints");
    return std::make_shared<const app::SdfAnnotationSummary>(endpoints,
        std::vector<app::SdfAnnotationTargetSummary> { }, count, count, 0U, 0U,
        0U, "secondary-summary");
}

fsim::app::SdfResolvedEndpoint endpoint(
    const fsim::app::SdfEndpointRole role,
    const fsim::runtime::simir::SignalId signal)
{
    fsim::app::SdfResolvedEndpoint result;
    result.role = role;
    result.object_kind = fsim::app::SdfEndpointObjectKind::HdlNet;
    result.instance_path = "top.u";
    result.object_path = signal == 0U ? "top.u.ref" : "top.u.data";
    result.signal = signal;
    result.object_width = 1U;
    result.language = fsim::app::SdfScopeRootLanguage::SystemVerilog;
    return result;
}

std::vector<fsim::app::SdfPlannedAnnotation> annotations()
{
    std::vector<fsim::app::SdfPlannedAnnotation> result;
    for (std::size_t index = 0; index < std::size(kinds); ++index) {
        fsim::app::SdfPlannedAnnotation item;
        item.node_id = index + 1U;
        item.construct_kind = kinds[index].construct;
        item.target_kind = fsim::app::SdfTimingTargetKind::TimingCheck;
        item.target_identity = "sdf:timingcheck:top.u:secondary:"
            + std::to_string(index);
        item.endpoints.push_back(
            endpoint(fsim::app::SdfEndpointRole::TimingReference, 0U));
        if (kinds[index].runtime
                != fsim::runtime::simir::ModuleTimingCheckKind::width
            && kinds[index].runtime
                != fsim::runtime::simir::ModuleTimingCheckKind::period) {
            item.endpoints.push_back(
                endpoint(fsim::app::SdfEndpointRole::TimingData, 1U));
        }
        const bool compound = kinds[index].runtime
                == fsim::runtime::simir::ModuleTimingCheckKind::fullskew
            || kinds[index].runtime
                == fsim::runtime::simir::ModuleTimingCheckKind::nochange;
        item.before_check_ticks = kinds[index].runtime
                == fsim::runtime::simir::ModuleTimingCheckKind::nochange
            ? std::vector<std::int64_t> { -5, 10 }
            : compound ? std::vector<std::int64_t> { 5, 10 }
                       : std::vector<std::int64_t> { 5 };
        item.after_check_ticks = kinds[index].runtime
                == fsim::runtime::simir::ModuleTimingCheckKind::nochange
            ? std::vector<std::int64_t> { -7, 12 }
            : compound ? std::vector<std::int64_t> { 7, 12 }
                       : std::vector<std::int64_t> { 7 };
        item.source = span();
        item.canonical_identity = "secondary-annotation-" + std::to_string(index);
        result.push_back(std::move(item));
    }
    return result;
}

std::shared_ptr<const fsim::app::SdfAnnotationPlan> plan(
    std::vector<fsim::app::SdfPlannedAnnotation> items)
{
    return std::make_shared<const fsim::app::SdfAnnotationPlan>(
        summary(items.size()), fsim::app::SdfValuePolicy { }, std::move(items),
        "secondary-plan");
}

void require_code(const fsim::app::SdfSecondaryTimingCheckResult& result,
    const std::string_view code)
{
    require(std::ranges::find(result.diagnostics, code,
                &fsim::frontend::Diagnostic::code)
            != result.diagnostics.end(),
        "missing secondary timing-check diagnostic");
}

void test_all_secondary_checks()
{
    const auto design = make_design();
    const auto result = fsim::app::apply_sdf_secondary_timing_checks(
        plan(annotations()), design);
    require(result.ok() && result.application->checks().size() == 6U,
        "all secondary timing-check kinds must publish");
    const auto* width = result.application->find_check(3U);
    require(width && width->effective_check.threshold == 2U
            && width->effective_check.event_based
            && width->effective_check.remain_active,
        "secondary overlays must retain threshold and runtime state");
    const auto* fullskew = result.application->find_check(2U);
    require(fullskew && fullskew->effective_limits == std::vector<std::int64_t>({ 7, 12 }),
        "fullskew must retain both limits");
}

void test_negative_and_resources()
{
    const auto design = make_design();
    auto items = annotations();
    items[5].after_check_ticks = { 12, 7 };
    auto result = fsim::app::apply_sdf_secondary_timing_checks(
        plan(std::move(items)), design);
    require(!result.ok(), "reversed nochange window must reject");
    require_code(result, "FSIM-SDF-SECONDARY-CHECK-003");

    items = annotations();
    items[0].endpoints[1].signal = 0U;
    result = fsim::app::apply_sdf_secondary_timing_checks(
        plan(std::move(items)), design);
    require(!result.ok(), "secondary event mismatch must reject");
    require_code(result, "FSIM-SDF-SECONDARY-CHECK-002");

    auto limits = fsim::app::SdfSecondaryTimingCheckLimits { };
    limits.max_checks = 5U;
    result = fsim::app::apply_sdf_secondary_timing_checks(
        plan(annotations()), design, limits);
    require(!result.ok() && !result.application,
        "secondary resource failure must reject atomically");
    require_code(result, "FSIM-SDF-SECONDARY-CHECK-004");
}
} // namespace

int main()
{
    try {
        test_all_secondary_checks();
        test_negative_and_resources();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}

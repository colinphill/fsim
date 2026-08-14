// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_delay_modes.hpp"

#include <iostream>
#include <limits>
#include <memory>
#include <ranges>
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
    result.source_name = "modes.sdf";
    result.begin = { 4U, 2U, 1U };
    result.end = { 12U, 2U, 9U };
    return result;
}

std::shared_ptr<const fsim::app::SdfAnnotationSummary> make_summary()
{
    using namespace fsim;
    auto ir = std::make_shared<const frontend::SdfIr>(
        frontend::SdfRevision::Sdf40, frontend::SdfRevisionAdapter::None,
        "modes.sdf", std::nullopt, std::vector<frontend::SdfIrCell> { },
        std::vector<frontend::SdfIrNode> { }, "modes-ir-identity");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir,
        "modes.sdf", "modes-source-identity", std::nullopt, '.', "project",
        "design", std::vector<app::SdfAnnotationRoot> { },
        "modes-scope-identity");
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { }, "modes-cells-identity");
    auto endpoints = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::vector<app::SdfResolvedNodeEndpoints> { },
        "modes-endpoint-identity");
    return std::make_shared<const app::SdfAnnotationSummary>(endpoints,
        std::vector<app::SdfAnnotationTargetSummary> { }, 1U, 0U, 1U, 0U, 0U,
        "modes-summary-identity");
}

std::shared_ptr<const fsim::app::SdfAnnotationPlan> make_plan(
    const fsim::app::SdfDelayApplicationMode mode,
    std::vector<std::uint64_t> before, std::vector<std::uint64_t> after,
    std::string identity = "path:top.u:A=>Z")
{
    fsim::app::SdfPlannedAnnotation annotation;
    annotation.node_id = 1U;
    annotation.cell_id = 1U;
    annotation.construct_kind = fsim::frontend::SdfConstructKind::Iopath;
    annotation.target_kind = fsim::app::SdfTimingTargetKind::SpecifyPath;
    annotation.delay_mode = mode;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = std::move(identity);
    annotation.before_ticks = std::move(before);
    annotation.after_ticks = std::move(after);
    annotation.source = source_span();
    annotation.source_identity = "modes-source";
    annotation.canonical_identity = "modes-annotation";
    return std::make_shared<const fsim::app::SdfAnnotationPlan>(make_summary(),
        fsim::app::SdfValuePolicy { },
        std::vector<fsim::app::SdfPlannedAnnotation> { std::move(annotation) },
        "modes-plan-identity");
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfDelayModeResult& result, const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error(
            "missing delay-mode diagnostic " + std::string { code });
    return *found;
}

void test_governed_expansion()
{
    using fsim::app::expand_sdf_transition_delays;
    auto profile = expand_sdf_transition_delays(
        std::vector<std::uint64_t> { 7U });
    require(profile && std::ranges::all_of(*profile, [](const auto value) { return value == 7U; }),
        "one value must expand to all transition classes");

    profile = expand_sdf_transition_delays(
        std::vector<std::uint64_t> { 10U, 20U });
    require(profile && (*profile)[0] == 10U && (*profile)[1] == 20U
            && (*profile)[2] == 10U && (*profile)[6] == 10U
            && (*profile)[7] == 10U && (*profile)[9] == 20U,
        "two values must derive turnoff and X transitions deterministically");

    profile = expand_sdf_transition_delays(
        std::vector<std::uint64_t> { 10U, 20U, 30U });
    require(profile && (*profile)[2] == 30U && (*profile)[4] == 30U
            && (*profile)[6] == 10U && (*profile)[10] == 30U,
        "three values must retain explicit turnoff fallback");

    profile = expand_sdf_transition_delays(
        std::vector<std::uint64_t> { 1U, 2U, 3U, 4U, 5U, 6U });
    require(profile && (*profile)[7] == 4U && (*profile)[8] == 2U
            && (*profile)[9] == 6U && (*profile)[11] == 4U,
        "six values must derive all X transitions with runtime-compatible min/max rules");

    std::vector<std::uint64_t> exact(12U);
    for (std::size_t index = 0; index < exact.size(); ++index)
        exact[index] = index + 1U;
    profile = expand_sdf_transition_delays(exact);
    require(profile && (*profile)[11] == 12U,
        "twelve values must retain every exact transition slot");
    require(!expand_sdf_transition_delays(
                std::vector<std::uint64_t> { 1U, 2U, 3U, 4U }),
        "unsupported arity must not expand");
}

void test_ordered_absolute_and_incremental_application()
{
    const std::vector<std::shared_ptr<const fsim::app::SdfAnnotationPlan>> plans {
        make_plan(fsim::app::SdfDelayApplicationMode::Absolute, { },
            { 10U, 20U }),
        make_plan(fsim::app::SdfDelayApplicationMode::Increment, { },
            { 1U, 2U, 3U }),
    };
    const auto result = fsim::app::apply_sdf_delay_modes(plans);
    require(result.ok() && result.application->steps().size() == 2U,
        "ordered absolute and incremental plans must publish both steps");
    const auto* final = result.application->find_final(
        fsim::app::SdfTimingTargetKind::SpecifyPath, "path:top.u:A=>Z");
    require(final != nullptr && final->plan_index == 1U
            && final->before_profile[0] == 10U
            && final->effective_profile[0] == 11U
            && final->effective_profile[1] == 22U
            && final->effective_profile[2] == 13U,
        "incremental application must add after prior absolute expansion in file order");

    const std::vector<std::shared_ptr<const fsim::app::SdfAnnotationPlan>> first_increment {
        make_plan(fsim::app::SdfDelayApplicationMode::Increment, { 5U },
            { 2U }),
    };
    const auto based = fsim::app::apply_sdf_delay_modes(first_increment);
    require(based.ok()
            && based.application->steps().front().effective_profile[11] == 7U,
        "first incremental application must use its exact declared baseline");
}

void test_stale_overflow_and_resource_rejection()
{
    std::vector<std::shared_ptr<const fsim::app::SdfAnnotationPlan>> plans {
        make_plan(fsim::app::SdfDelayApplicationMode::Absolute, { }, { 10U }),
        make_plan(fsim::app::SdfDelayApplicationMode::Increment, { 9U },
            { 1U }),
    };
    auto result = fsim::app::apply_sdf_delay_modes(plans);
    require(!result.ok() && !result.application,
        "stale repeated-file before-values must reject atomically");
    require_diagnostic(result, "FSIM-SDF-DELAY-MODE-003");

    plans = { make_plan(fsim::app::SdfDelayApplicationMode::Increment,
        { std::numeric_limits<std::uint64_t>::max() }, { 1U }) };
    result = fsim::app::apply_sdf_delay_modes(plans);
    require(!result.ok(), "incremental tick overflow must reject atomically");
    require_diagnostic(result, "FSIM-SDF-DELAY-MODE-004");

    auto limits = fsim::app::SdfDelayModeLimits { };
    limits.max_steps = 1U;
    plans = { make_plan(fsim::app::SdfDelayApplicationMode::Absolute, { },
                  { 1U }, "path:first"),
        make_plan(fsim::app::SdfDelayApplicationMode::Absolute, { }, { 2U },
            "path:second") };
    result = fsim::app::apply_sdf_delay_modes(plans, limits);
    require(!result.ok(), "step resource boundary must reject atomically");
    require_diagnostic(result, "FSIM-SDF-DELAY-MODE-004");
}
} // namespace

int main()
{
    try {
        test_governed_expansion();
        test_ordered_absolute_and_incremental_application();
        test_stale_overflow_and_resource_rejection();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}

// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_precedence.hpp"

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
    result.source_name = "vital-precedence.sdf";
    result.begin = { offset, 3U, offset + 1U };
    result.end = { offset + 1U, 3U, offset + 2U };
    return result;
}

std::shared_ptr<const fsim::app::SdfVitalModelPlan> delay_plan(
    std::string identity, const std::vector<std::uint64_t>& before,
    const std::vector<std::uint64_t>& after,
    const fsim::app::SdfDelayApplicationMode mode,
    const fsim::app::SdfDelaySelection selection
    = fsim::app::SdfDelaySelection::Typical)
{
    using namespace fsim::app;
    SdfVitalPathTimingRecord path;
    path.node_id = 1U;
    path.cell_id = 1U;
    path.construct_kind = fsim::frontend::SdfConstructKind::Iopath;
    path.annotation_mode = mode;
    path.instance_path = "top.u";
    path.call.kind = SdfVitalCallKind::Delay;
    path.call.process = 4U;
    path.call.instruction = 9U;
    path.call.canonical_identity = "stable-vital-delay-call";
    path.before_delay_ticks = before;
    path.source = span(10U);
    path.source_identity = identity + "-source";
    for (std::size_t index = 0; index < after.size(); ++index) {
        SdfSelectedDelay selected;
        selected.selection = selection;
        selected.ticks = after[index];
        selected.canonical_identity
            = identity + "-delay-" + std::to_string(index);
        path.after_delays.push_back(std::move(selected));
    }
    path.canonical_identity = identity + "-path";
    SdfValuePolicy value_policy;
    value_policy.selection = selection;
    auto paths = std::make_shared<const SdfVitalPathTimingPlan>(nullptr,
        value_policy, std::vector<SdfVitalPathTimingRecord> { path },
        identity + "-paths");
    SdfVitalModelRecord model;
    model.node_id = path.node_id;
    model.cell_id = path.cell_id;
    model.instance_path = path.instance_path;
    model.call = path.call;
    model.path_identity = path.canonical_identity;
    model.target_identity = "stable-vital-target";
    model.canonical_identity = identity + "-model-record";
    return std::make_shared<const SdfVitalModelPlan>(paths,
        std::vector<SdfVitalWrapperRegistration> { },
        std::vector<SdfVitalModelRecord> { std::move(model) },
        std::move(identity));
}

std::shared_ptr<const fsim::app::SdfVitalModelPlan> check_plan(
    std::string identity, const std::vector<std::int64_t>& before,
    const std::vector<std::int64_t>& after,
    const fsim::app::SdfDelaySelection selection
    = fsim::app::SdfDelaySelection::Typical)
{
    using namespace fsim::app;
    SdfVitalPathTimingRecord path;
    path.node_id = 2U;
    path.cell_id = 1U;
    path.construct_kind = fsim::frontend::SdfConstructKind::SetupHold;
    path.annotation_mode = SdfDelayApplicationMode::None;
    path.instance_path = "top.u";
    path.call.kind = SdfVitalCallKind::TimingCheck;
    path.call.process = 5U;
    path.call.instruction = 2U;
    path.call.canonical_identity = "stable-vital-check-call";
    path.before_check_ticks = before;
    path.source = span(20U);
    path.source_identity = identity + "-source";
    for (std::size_t index = 0; index < after.size(); ++index) {
        SdfSelectedTimingCheckValue selected;
        selected.selection = selection;
        selected.ticks = after[index];
        selected.canonical_identity
            = identity + "-check-" + std::to_string(index);
        path.after_checks.push_back(std::move(selected));
    }
    path.canonical_identity = identity + "-path";
    SdfValuePolicy value_policy;
    value_policy.selection = selection;
    auto paths = std::make_shared<const SdfVitalPathTimingPlan>(nullptr,
        value_policy, std::vector<SdfVitalPathTimingRecord> { path },
        identity + "-paths");
    SdfVitalModelRecord model;
    model.node_id = path.node_id;
    model.cell_id = path.cell_id;
    model.instance_path = path.instance_path;
    model.call = path.call;
    model.path_identity = path.canonical_identity;
    model.target_identity = "stable-vital-check-target";
    model.canonical_identity = identity + "-model-record";
    return std::make_shared<const SdfVitalModelPlan>(paths,
        std::vector<SdfVitalWrapperRegistration> { },
        std::vector<SdfVitalModelRecord> { std::move(model) },
        std::move(identity));
}

void require_diagnostic(const fsim::app::SdfVitalPrecedenceResult& result,
    const std::string_view code)
{
    require(std::ranges::any_of(result.diagnostics,
                [&](const auto& diagnostic) { return diagnostic.code == code; }),
        "expected VITAL precedence diagnostic was not emitted");
}

void test_no_annotation_and_timing_generic_identity()
{
    const auto source = delay_plan("source", { 10U, 20U }, { },
        fsim::app::SdfDelayApplicationMode::None);
    fsim::app::SdfVitalTimingGenericValue generic;
    generic.call_identity = "stable-vital-delay-call";
    generic.value_index = 0U;
    generic.delay_ticks = 15U;
    generic.generic_identity = "tpd-a-z:vhdl-time:15";
    const std::array generics { generic };
    const auto result = fsim::app::apply_sdf_vital_precedence(
        source, { }, generics);
    require(result.ok() && result.application->values().size() == 2U,
        "source-only VITAL precedence should retain every delay value");
    require(result.application->values()[0].effective_delay_ticks == 15U
            && result.application->values()[0].base_source
                == fsim::app::SdfVitalEffectiveValueSource::TimingGeneric
            && result.application->values()[1].effective_delay_ticks == 20U,
        "timing generic should override only its exact source-record value");
    require(std::ranges::all_of(result.application->values(),
                &fsim::app::SdfVitalEffectiveTimingValue::no_annotation)
            && result.application->semantic_identity().find("no-annotation")
                != std::string_view::npos,
        "source-only precedence should publish stable no-annotation identity");
}

void test_absolute_increment_repeated_file_order()
{
    using namespace fsim::app;
    const auto source = delay_plan("source", { 10U, 20U }, { },
        SdfDelayApplicationMode::None, SdfDelaySelection::Minimum);
    const auto absolute = delay_plan("absolute", { 10U, 20U }, { 100U, 200U },
        SdfDelayApplicationMode::Absolute, SdfDelaySelection::Minimum);
    const auto increment = delay_plan("increment", { 10U, 20U }, { 3U, 4U },
        SdfDelayApplicationMode::Increment, SdfDelaySelection::Minimum);
    const std::array revisions { absolute, increment, increment };
    SdfVitalPrecedencePolicy policy;
    policy.command_selection = SdfDelaySelection::Minimum;
    const auto result = apply_sdf_vital_precedence(
        source, revisions, { }, { }, policy);
    require(result.ok() && result.application->values()[0].effective_delay_ticks == 106U
            && result.application->values()[1].effective_delay_ticks == 208U,
        "absolute then repeated increments should apply in file order");
    require(result.application->values()[0].steps.size() == 3U
            && result.application->values()[0].selected_source
                == SdfVitalEffectiveValueSource::SdfIncrement
            && result.application->values()[0].command_selection
                == SdfDelaySelection::Minimum,
        "precedence should retain revision order and exact selection");
}

void test_disable_controls_and_signed_checks()
{
    using namespace fsim::app;
    const auto source = delay_plan("source", { 10U }, { },
        SdfDelayApplicationMode::None);
    const auto absolute = delay_plan(
        "absolute", { 10U }, { 100U }, SdfDelayApplicationMode::Absolute);
    const auto increment = delay_plan(
        "increment", { 10U }, { 3U }, SdfDelayApplicationMode::Increment);
    const std::array revisions { absolute, increment };
    SdfVitalAnnotationControl control;
    control.revision_index = 0U;
    control.plan_identity = absolute->semantic_identity();
    control.delay_enabled = false;
    control.control_identity = "disable-first-file";
    const std::array controls { control };
    auto result = apply_sdf_vital_precedence(
        source, revisions, { }, controls);
    require(result.ok()
            && result.application->values()[0].effective_delay_ticks == 13U
            && !result.application->values()[0].steps[0].enabled
            && result.application->values()[0].steps[1].enabled,
        "disabled file should remain auditable without changing precedence");

    const auto check_source = check_plan("check-source", { 5 }, { });
    const auto check_revision = check_plan("check-revision", { 5 }, { -2 });
    const std::array check_revisions { check_revision };
    result = apply_sdf_vital_precedence(check_source, check_revisions);
    require(result.ok()
            && result.application->values()[0].effective_check_ticks == -2
            && result.application->values()[0].steps[0].mode
                == SdfDelayApplicationMode::Absolute,
        "timing checks should retain signed values and implicit absolute mode");

    SdfVitalPrecedencePolicy disabled;
    disabled.timing_check_annotations_enabled = false;
    result = apply_sdf_vital_precedence(
        check_source, check_revisions, { }, { }, disabled);
    require(result.ok()
            && result.application->values()[0].effective_check_ticks == 5
            && result.application->values()[0].no_annotation,
        "global timing-check disable should preserve source behavior identity");
}

void test_atomic_selection_generic_mode_control_and_resource_rejection()
{
    using namespace fsim::app;
    const auto source = delay_plan("source", { 10U }, { },
        SdfDelayApplicationMode::None);
    const auto wrong_selection = delay_plan("wrong-selection", { 10U }, { 20U },
        SdfDelayApplicationMode::Absolute, SdfDelaySelection::Maximum);
    const std::array wrong_revisions { wrong_selection };
    auto result = apply_sdf_vital_precedence(source, wrong_revisions);
    require(!result.ok() && result.application == nullptr,
        "selection mismatch must reject the complete precedence result");
    require_diagnostic(result, "FSIM-SDF-VITAL-PRECEDENCE-002");

    SdfVitalTimingGenericValue generic;
    generic.call_identity = "stable-vital-delay-call";
    generic.delay_ticks = 11U;
    generic.generic_identity = "generic-11";
    const std::array duplicate_generics { generic, generic };
    result = apply_sdf_vital_precedence(
        source, { }, duplicate_generics);
    require(!result.ok() && result.application == nullptr,
        "duplicate timing generic must reject atomically");
    require_diagnostic(result, "FSIM-SDF-VITAL-PRECEDENCE-003");

    const auto missing_mode = delay_plan(
        "missing-mode", { 10U }, { 20U }, SdfDelayApplicationMode::None);
    const std::array missing_mode_revisions { missing_mode };
    result = apply_sdf_vital_precedence(source, missing_mode_revisions);
    require(!result.ok() && result.application == nullptr,
        "delay revision without absolute/increment mode must reject atomically");
    require_diagnostic(result, "FSIM-SDF-VITAL-PRECEDENCE-004");

    SdfVitalAnnotationControl control;
    control.revision_index = 0U;
    control.plan_identity = "stale-plan";
    control.control_identity = "stale-control";
    const std::array controls { control };
    result = apply_sdf_vital_precedence(
        source, missing_mode_revisions, { }, controls);
    require(!result.ok() && result.application == nullptr,
        "stale disable control must reject atomically");
    require_diagnostic(result, "FSIM-SDF-VITAL-PRECEDENCE-005");

    const auto absolute = delay_plan(
        "absolute", { 10U }, { 20U }, SdfDelayApplicationMode::Absolute);
    const std::array repeated { absolute, absolute };
    SdfVitalPrecedenceLimits limits;
    limits.max_steps_per_value = 1U;
    result = apply_sdf_vital_precedence(
        source, repeated, { }, { }, { }, limits);
    require(!result.ok() && result.application == nullptr,
        "revision-step overflow must reject atomically");
    require_diagnostic(result, "FSIM-SDF-VITAL-PRECEDENCE-006");

    limits = { };
    limits.max_values = 0U;
    result = apply_sdf_vital_precedence(
        source, { }, { }, { }, { }, limits);
    require(!result.ok() && result.application == nullptr,
        "zero precedence limits must reject atomically");
    require_diagnostic(result, "FSIM-SDF-VITAL-PRECEDENCE-001");
}
} // namespace

int main()
{
    try {
        test_no_annotation_and_timing_generic_identity();
        test_absolute_increment_repeated_file_order();
        test_disable_controls_and_signed_checks();
        test_atomic_selection_generic_mode_control_and_resource_rejection();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}

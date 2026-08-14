// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_observability.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error { std::string { message } };
}

void require_diagnostic(
    const std::vector<fsim::frontend::Diagnostic>& diagnostics,
    const std::string_view code)
{
    require(std::ranges::any_of(diagnostics, [&](const auto& diagnostic) {
        return diagnostic.code == code;
    }),
        "expected cataloged SDF observation diagnostic");
}

fsim::frontend::SourceSpan span(
    std::string source, const std::size_t line, const std::size_t column)
{
    fsim::frontend::SourceSpan result;
    result.source_name = std::move(source);
    result.begin = { 0U, line, column };
    result.end = { 1U, line, column + 1U };
    return result;
}

std::vector<fsim::app::SdfObservationTarget> targets()
{
    using namespace fsim::app;
    return {
        { SdfEffectiveValueKind::TimingCheckLimit, "top.u0:setuphold:0",
            "timing/checks.sdf", span("timing/checks.sdf", 31U, 4U),
            { 2, 3 }, { 7, 8 } },
        { SdfEffectiveValueKind::PathDelay, "top.u0:a->q", "timing/slow.sdf",
            span("timing/slow.sdf", 17U, 9U), { 1, 2, 3 }, { 11, 12, 13 } }
    };
}

void test_stable_catalog_and_public_objects()
{
    using namespace fsim::app;
    const auto first = build_sdf_observability(targets());
    auto reordered = targets();
    std::ranges::reverse(reordered);
    const auto second = build_sdf_observability(reordered);
    require(first.ok() && second.ok(),
        "valid effective targets must publish an observation catalog");
    require(first.application->semantic_identity()
            == second.application->semantic_identity(),
        "observation identity must be input-order independent");
    require(first.application->objects().size() == 2U
            && first.application->objects()[0].stable_id == 1U
            && first.application->objects()[1].stable_id == 2U,
        "annotated object enumeration must use stable lexical IDs");
    const auto* path = first.application->find_object("top.u0:a->q");
    require(path && path->original_values == std::vector<std::int64_t>({ 1, 2, 3 })
            && path->effective_values
                == std::vector<std::int64_t>({ 11, 12, 13 })
            && path->source_span.begin.line == 17U
            && path->vpi_handle != 0U && path->vcd_name == "sdf_top_u0_a__q",
        "debugger/VPI/VCD object metadata must retain values and source span");
}

void test_surface_ordering_regions_and_values()
{
    using namespace fsim::app;
    const auto built = build_sdf_observability(targets());
    constexpr std::array surfaces { SdfObservationSurface::Debugger,
        SdfObservationSurface::Callback,
        SdfObservationSurface::InternalTrace, SdfObservationSurface::Vpi,
        SdfObservationSurface::Vcd };
    SdfObservationRecorder recorder { built.application, surfaces, 3U };
    require(recorder.enabled() && recorder.reserved_event_slots() == 15U,
        "enabled surfaces must reserve all bounded storage before runtime");
    const std::array<std::int64_t, 2> before { 2, 3 };
    const std::array<std::int64_t, 2> after { 7, 8 };
    require(recorder.record_violation("top.u0:setuphold:0", 9U, 2U,
                fsim::runtime::SchedulerPhase::observed, before, after)
                == SdfObservationStatus::Recorded
            && recorder.record_violation("top.u0:setuphold:0", 10U, 0U,
                   fsim::runtime::SchedulerPhase::reactive, before, after)
                == SdfObservationStatus::Recorded,
        "valid violations must publish without changing scheduling");
    const std::array views { recorder.debugger_events(),
        recorder.callback_events(), recorder.internal_trace_events(),
        recorder.vpi_events(), recorder.vcd_events() };
    for (const auto view : views) {
        require(view.size() == 2U && view[0].sequence == 1U
                && view[1].sequence == 2U
                && view[0].region == fsim::runtime::SchedulerPhase::observed
                && view[1].region == fsim::runtime::SchedulerPhase::reactive
                && view[0].value_count == 2U && view[0].before[0] == 2
                && view[0].after[1] == 8,
            "all observation surfaces must preserve callback region and trace order");
    }
}

void test_disabled_and_bounded_hot_path()
{
    using namespace fsim::app;
    const auto built = build_sdf_observability(targets());
    const std::span<const SdfObservationSurface> no_surfaces;
    SdfObservationRecorder disabled { built.application, no_surfaces, 4U };
    const std::array<std::int64_t, 1> value { 1 };
    require(!disabled.enabled() && disabled.reserved_event_slots() == 0U,
        "disabled observation must allocate no event storage");
    require(disabled.record_violation("top.u0:a->q", 1U, 0U,
                fsim::runtime::SchedulerPhase::active, value, value)
                == SdfObservationStatus::Disabled
            && disabled.reserved_event_slots() == 0U,
        "disabled hot path must return before allocation or lookup");

    constexpr std::array callbacks { SdfObservationSurface::Callback };
    SdfObservationRecorder bounded { built.application, callbacks, 1U };
    require(bounded.record_violation("top.u0:a->q", 1U, 0U,
                fsim::runtime::SchedulerPhase::active, value, value)
                == SdfObservationStatus::Recorded
            && bounded.record_violation("top.u0:a->q", 2U, 0U,
                   fsim::runtime::SchedulerPhase::active, value, value)
                == SdfObservationStatus::LimitReached
            && bounded.callback_events().size() == 1U
            && bounded.debugger_events().empty(),
        "bounded callback reporting must reject overflow without partial fanout");
    require(bounded.record_violation("missing", 3U, 0U,
                fsim::runtime::SchedulerPhase::active, value, value)
            == SdfObservationStatus::UnknownTarget,
        "unknown annotated object must reject before publication");
    require(sdf_observation_status_diagnostic_code(
                SdfObservationStatus::UnknownTarget)
            == "FSIM-SDF-OBSERVE-003",
        "dynamic rejection must map to the cataloged diagnostic family");
    const std::array<std::int64_t, 2> too_many { 1, 2 };
    require(bounded.record_violation("top.u0:a->q", 3U, 0U,
                static_cast<fsim::runtime::SchedulerPhase>(255), too_many,
                value)
            == SdfObservationStatus::InvalidEvent,
        "invalid region or before/after arity must reject");
}

void test_setup_negatives_and_resources()
{
    using namespace fsim::app;
    auto duplicate = targets();
    duplicate.push_back(duplicate.front());
    const auto duplicated = build_sdf_observability(duplicate);
    require(!duplicated.ok(), "duplicate observed target must reject");
    require_diagnostic(duplicated.diagnostics, "FSIM-SDF-OBSERVE-002");

    auto invalid = targets();
    invalid.front().source_span.source_name.clear();
    const auto invalid_result = build_sdf_observability(invalid);
    require(!invalid_result.ok(), "missing source span must reject");
    require_diagnostic(invalid_result.diagnostics, "FSIM-SDF-OBSERVE-001");

    SdfObservabilityLimits limits;
    limits.max_objects = 1U;
    const auto limited = build_sdf_observability(targets(), limits);
    require(!limited.ok(), "object resource limit must reject");
    require_diagnostic(limited.diagnostics, "FSIM-SDF-OBSERVE-004");
}

} // namespace

int main()
{
    try {
        test_stable_catalog_and_public_objects();
        test_surface_ordering_regions_and_values();
        test_disabled_and_bounded_hot_path();
        test_setup_negatives_and_resources();
        std::cout << "SDF observability tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}

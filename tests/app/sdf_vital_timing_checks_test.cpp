// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_timing_checks.hpp"

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
    result.source_name = "vital-checks.sdf";
    result.begin = { offset, 4U, offset + 1U };
    result.end = { offset + 1U, 4U, offset + 2U };
    return result;
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    using namespace runtime::simir;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    for (const auto name : { "top.test", "top.reference" }) {
        const auto id = static_cast<SignalId>(state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 1U;
        info.type_name = "std_ulogic";
        info.source_domain = frontend::ValueDomain::Logic9;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(std::string { name },
            runtime::PackedLogic4::from_logic9_msb_string("0"),
            ResolutionKind::none, ValueKind::logic9);
        state.signal_names.emplace_back(name, id);
    }
    constexpr std::array kinds {
        VitalTimingCheckKind::setup_hold,
        VitalTimingCheckKind::recovery_removal,
        VitalTimingCheckKind::period_pulse,
        VitalTimingCheckKind::in_phase_skew,
        VitalTimingCheckKind::out_phase_skew,
    };
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        Process process;
        process.id = static_cast<ProcessId>(index);
        process.name = "top.vital-check-" + std::to_string(index);
        process.language_standard = "vhdl-2008";
        process.register_count = 1U;
        process.register_value_kinds = { ValueKind::logic9 };
        VitalTimingCheck check;
        check.destination = 0U;
        check.kind = kinds[index];
        check.test_signal = 0U;
        if (check.kind != VitalTimingCheckKind::period_pulse)
            check.reference_signal = 1U;
        check.limits = { 3U + index, 5U + index, 7U + index,
            9U + index };
        check.check_enabled = index != 1U;
        check.enables = { true, false, true, index != 3U };
        check.x_on = index != 4U;
        check.message_on = index != 3U;
        check.severity = AssertionSeverity::error;
        check.message = "check-message-" + std::to_string(index);
        check.source = { "cell.vhd",
            static_cast<std::uint32_t>(40U + index), 6U };
        process.operations.emplace_back(std::move(check));
        process.operations.emplace_back(Halt { });
        state.processes.push_back(std::move(process));
    }
    auto design
        = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "VITAL timing-check design must validate");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfVitalModelPlan> make_model_plan(
    const fsim::elaboration::ElaboratedDesign& design,
    const std::string& identity, const bool revision,
    const std::int64_t replacement)
{
    using namespace fsim;
    using namespace app;
    std::vector<SdfVitalPathTimingRecord> paths;
    std::vector<SdfVitalModelRecord> models;
    std::uint64_t node_id = 1U;
    for (const auto& process : design.processes()) {
        for (std::size_t instruction = 0;
            instruction < process.operations.size(); ++instruction) {
            const auto* check = runtime::simir::operation_get_if<
                runtime::simir::VitalTimingCheck>(
                &process.operations[instruction]);
            if (check == nullptr)
                continue;
            SdfVitalPathTimingRecord path;
            path.node_id = node_id;
            path.cell_id = node_id;
            path.construct_kind = frontend::SdfConstructKind::SetupHold;
            path.instance_path = "top";
            path.call.process = process.id;
            path.call.instruction = static_cast<std::uint32_t>(instruction);
            path.call.kind = SdfVitalCallKind::TimingCheck;
            path.call.source = check->source;
            path.call.canonical_identity
                = "vital-check-call-" + std::to_string(process.id);
            path.endpoint_signals = { check->test_signal };
            if (check->reference_signal)
                path.endpoint_signals.push_back(*check->reference_signal);
            path.edge_identities = { "test-edge", "reference-edge" };
            path.condition_identity = "check-enable";
            path.source = span(static_cast<std::size_t>(node_id) * 10U);
            path.source_identity
                = identity + "-source-" + std::to_string(node_id);
            for (std::size_t index = 0; index < check->limits.size(); ++index) {
                path.before_check_ticks.push_back(
                    static_cast<std::int64_t>(check->limits[index]));
                SdfSelectedTimingCheckValue selected;
                selected.selection = SdfDelaySelection::Typical;
                selected.ticks = revision
                    ? replacement + static_cast<std::int64_t>(index)
                    : static_cast<std::int64_t>(check->limits[index]);
                selected.canonical_identity = identity + "-value-"
                    + std::to_string(node_id) + '-' + std::to_string(index);
                path.after_checks.push_back(std::move(selected));
            }
            path.canonical_identity
                = identity + "-path-" + std::to_string(node_id);
            SdfVitalModelRecord model;
            model.node_id = node_id;
            model.cell_id = node_id;
            model.instance_path = path.instance_path;
            model.owned_processes = { process.id };
            model.call = path.call;
            model.target_identity
                = identity + "-target-" + std::to_string(node_id);
            model.path_identity = path.canonical_identity;
            model.source = path.source;
            model.canonical_identity
                = identity + "-model-" + std::to_string(node_id);
            models.push_back(std::move(model));
            paths.push_back(std::move(path));
            ++node_id;
        }
    }
    app::SdfValuePolicy policy;
    policy.selection = app::SdfDelaySelection::Typical;
    auto path_plan = std::make_shared<const app::SdfVitalPathTimingPlan>(
        nullptr, policy, std::move(paths), identity + "-paths");
    return std::make_shared<const app::SdfVitalModelPlan>(path_plan,
        std::vector<app::SdfVitalWrapperRegistration> { }, std::move(models),
        identity);
}

std::shared_ptr<const fsim::app::SdfVitalSchedulingApplication>
make_scheduling(const fsim::elaboration::ElaboratedDesign& design)
{
    using namespace fsim::app;
    const auto source = make_model_plan(design, "source", false, 20);
    const auto revision = make_model_plan(design, "revision", true, 20);
    const std::array revisions { revision };
    const auto precedence = apply_sdf_vital_precedence(
        source, revisions, { }, { }, { });
    require(precedence.ok(), "VITAL timing-check precedence must publish");
    const auto scheduling
        = apply_sdf_vital_scheduling(precedence.application, design);
    require(scheduling.ok(), "VITAL delay scheduling with check-only input must publish");
    return scheduling.application;
}

void require_diagnostic(const fsim::app::SdfVitalTimingCheckResult& result,
    const std::string_view code)
{
    require(!result.ok()
            && std::ranges::any_of(result.diagnostics,
                [&](const auto& diagnostic) {
                    return diagnostic.code == code;
                }),
        "expected VITAL timing-check diagnostic was not emitted");
}

void test_publication_state_and_violations()
{
    using namespace fsim;
    using namespace runtime::simir;
    const auto design = make_design();
    const auto scheduling = make_scheduling(design);
    const auto result = app::apply_sdf_vital_timing_checks(scheduling);
    require(result.ok() && result.application->checks().size() == 5U,
        "all five VITAL timing-check kinds must publish");
    const auto& checks = result.application->checks();
    require(checks[0].kind == VitalTimingCheckKind::setup_hold
            && checks[1].kind == VitalTimingCheckKind::recovery_removal
            && checks[2].kind == VitalTimingCheckKind::period_pulse
            && checks[3].kind == VitalTimingCheckKind::in_phase_skew
            && checks[4].kind == VitalTimingCheckKind::out_phase_skew
            && checks[0].effective_limits
                == std::array<std::uint64_t, 4> { 20U, 21U, 22U, 23U }
            && !checks[1].check_enabled && !checks[3].message_on
            && !checks[4].x_on
            && checks[0].severity == AssertionSeverity::error
            && checks[0].message == "check-message-0"
            && checks[0].violation_source.path == "cell.vhd"
            && checks[0].annotation_source.source_name
                == "vital-checks.sdf",
        "scheduled checks must retain kinds, limits, enables and coordinates");
    const auto* setup = operation_get_if<VitalTimingCheck>(
        &result.application->design().processes()[0].operations[0]);
    const auto* period = operation_get_if<VitalTimingCheck>(
        &result.application->design().processes()[2].operations[0]);
    const auto* original_setup = operation_get_if<VitalTimingCheck>(
        &design.processes()[0].operations[0]);
    require(setup != nullptr && period != nullptr && original_setup != nullptr
            && setup->limits == checks[0].effective_limits
            && original_setup->limits[0] == 3U
            && setup->limits[0] == 20U,
        "timing-check publication must rewrite only the copied design");

    runtime::simir::Interpreter interpreter { { 1000U, 32U } };
    const auto test = interpreter.add_signal({ "test",
        runtime::PackedLogic4::from_logic9_msb_string("0"),
        ResolutionKind::none, ValueKind::logic9 });
    const auto violation = interpreter.add_signal({ "violation",
        runtime::PackedLogic4::from_logic9_msb_string("0"),
        ResolutionKind::none, ValueKind::logic9 });
    Process checker;
    checker.id = 0U;
    checker.name = "annotated-period-pulse";
    checker.register_count = 1U;
    checker.register_value_kinds = { ValueKind::logic9 };
    checker.static_sensitivity = { { test, EdgeKind::any } };
    checker.driver_regions = { { violation, 0U, 1U, true } };
    auto runtime_check = *period;
    runtime_check.destination = 0U;
    runtime_check.test_signal = test;
    runtime_check.message = "annotated period/pulse violation";
    checker.operations = { runtime_check, WriteUpdate { violation, 0U },
        WaitSensitivity { }, Jump { 0U } };
    (void)interpreter.add_process(std::move(checker));
    Process stimulus;
    stimulus.id = 1U;
    stimulus.name = "timing-stimulus";
    stimulus.register_count = 2U;
    stimulus.register_value_kinds = { ValueKind::logic9, ValueKind::logic9 };
    stimulus.driver_regions = { { test, 0U, 1U, true } };
    stimulus.operations = {
        LoadConstant { 0U,
            runtime::PackedLogic4::from_logic9_msb_string("1") },
        LoadConstant { 1U,
            runtime::PackedLogic4::from_logic9_msb_string("0") },
        WriteAfter { test, 0U, 5U }, WriteAfter { test, 1U, 10U },
        WaitFor { 15U }, Halt { }
    };
    (void)interpreter.add_process(std::move(stimulus));
    std::vector<std::pair<std::uint64_t, std::string>> changes;
    std::size_t reports { };
    interpreter.set_signal_change_hook(
        [&](const SignalId signal, const runtime::PackedLogic4& value,
            const std::uint64_t time) {
            if (signal == violation)
                changes.emplace_back(time, value.to_msb_string());
        });
    interpreter.set_report_hook(
        [&](ProcessId, const std::string_view message, AssertionSeverity,
            const SourceLocation&, std::uint64_t, std::uint64_t) {
            if (message == "annotated period/pulse violation")
                ++reports;
        });
    interpreter.start();
    (void)interpreter.run();
    require(changes == std::vector<std::pair<std::uint64_t, std::string>> { { 10U, "X" } }
            && reports == 1U,
        "annotated period/pulse violation must retain exact time and message");
}

void test_atomic_negative_and_resource_rejection()
{
    using namespace fsim::app;
    const auto design = make_design();
    const auto scheduling = make_scheduling(design);
    auto values = std::vector<SdfVitalEffectiveTimingValue> {
        scheduling->precedence()->values().begin(),
        scheduling->precedence()->values().end()
    };
    const auto found = std::ranges::find_if(
        values, [](const auto& value) { return value.timing_check; });
    require(found != values.end(), "negative fixture must find a timing check");
    found->effective_check_ticks = -1;
    auto corrupt_precedence
        = std::make_shared<const SdfVitalPrecedenceApplication>(
            scheduling->precedence()->source(),
            std::vector<std::shared_ptr<const SdfVitalModelPlan>> {
                scheduling->precedence()->revisions().begin(),
                scheduling->precedence()->revisions().end() },
            std::vector<SdfVitalTimingGenericValue> { },
            std::vector<SdfVitalAnnotationControl> { },
            scheduling->precedence()->policy(), std::move(values),
            "negative-check-precedence");
    auto corrupt_scheduling
        = std::make_shared<const SdfVitalSchedulingApplication>(
            corrupt_precedence, scheduling->design(),
            std::vector<SdfVitalScheduledDelay> { },
            "negative-check-scheduling");
    require_diagnostic(apply_sdf_vital_timing_checks(corrupt_scheduling),
        "FSIM-SDF-VITAL-CHECK-003");
    require_diagnostic(apply_sdf_vital_timing_checks(
                           scheduling, { 5U, 1U, 1024U }),
        "FSIM-SDF-VITAL-CHECK-006");
    require_diagnostic(apply_sdf_vital_timing_checks(
                           scheduling, { 0U, 1U, 1024U }),
        "FSIM-SDF-VITAL-CHECK-001");
}
} // namespace

int main()
{
    try {
        test_publication_state_and_violations();
        test_atomic_negative_and_resource_rejection();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

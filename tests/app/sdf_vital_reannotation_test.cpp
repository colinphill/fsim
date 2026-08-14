// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/sdf_vital_reannotation.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(const bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error { message };
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    using namespace runtime::simir;
    elaboration::ElaboratedDesignState state;
    state.top = "alpha";
    state.roots = { "alpha", "beta" };
    constexpr std::array names {
        "alpha.u0.in", "alpha.u1.in", "beta.u0.in", "beta.u1.in",
        "alpha.u0.out", "alpha.u1.out", "beta.u0.out", "beta.u1.out",
        "alpha.u2.check", "control"
    };
    for (const auto name : names) {
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
    for (ProcessId process_id = 0U; process_id < 4U; ++process_id) {
        Process process;
        process.id = process_id;
        process.name = names[process_id];
        process.language_standard = "vhdl-2008";
        process.register_count = 7U;
        process.register_value_kinds.assign(7U, ValueKind::logic4);
        process.register_value_kinds[0] = ValueKind::logic9;
        process.static_sensitivity = { { process_id, EdgeKind::any } };
        process.driver_regions = { { process_id + 4U, 0U, 1U, true } };
        process.operations.emplace_back(ReadSignal { 0U, process_id });
        for (RegisterId delay_index = 1U; delay_index < 7U;
            ++delay_index) {
            process.operations.emplace_back(LoadConstant { delay_index,
                runtime::PackedLogic4::from_aval_bval(
                    64U, delay_index == 1U ? 5U : 0U, 0U) });
        }
        VitalDelay delay;
        delay.kind = VitalDelayKind::path;
        delay.shape = VitalDelayShape::single;
        delay.output = process_id + 4U;
        delay.source = 0U;
        delay.default_delays = { 1U, 2U, 3U, 4U, 5U, 6U };
        delay.mode = VitalGlitchMode::transport;
        delay.source_location
            = { "fixture.vhd", 10U + process_id, 3U };
        process.operations.emplace_back(std::move(delay));
        process.operations.emplace_back(WaitSensitivity { });
        process.operations.emplace_back(Jump { 0U });
        state.processes.push_back(std::move(process));
    }
    Process checker;
    checker.id = 4U;
    checker.name = "alpha.u2.check";
    checker.language_standard = "vhdl-2008";
    checker.register_count = 1U;
    checker.register_value_kinds = { ValueKind::logic9 };
    checker.static_sensitivity = { { 8U, EdgeKind::any } };
    VitalTimingCheck check;
    check.destination = 0U;
    check.kind = VitalTimingCheckKind::period_pulse;
    check.test_signal = 8U;
    check.limits = { 10U, 0U, 0U, 0U };
    check.message = "live VITAL period violation";
    check.source = { "fixture.vhd", 30U, 3U };
    checker.operations.emplace_back(std::move(check));
    checker.operations.emplace_back(WaitSensitivity { });
    checker.operations.emplace_back(Jump { 0U });
    state.processes.push_back(std::move(checker));
    auto design
        = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "VITAL live design must validate");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfVitalTimingCheckApplication>
make_application(const fsim::elaboration::ElaboratedDesign& design,
    const std::array<std::uint64_t, 4>& delay_values,
    const std::uint64_t check_value, const std::string& identity,
    const bool with_generics = false)
{
    using namespace fsim;
    using namespace app;
    constexpr std::array instances {
        "alpha.u0", "alpha.u1", "beta.u0", "beta.u1", "alpha.u2"
    };
    std::vector<SdfVitalPathTimingRecord> paths;
    std::vector<SdfVitalModelRecord> models;
    std::vector<SdfVitalScheduledDelay> delays;
    for (std::size_t index = 0; index < 4U; ++index) {
        SdfVitalCallReference call;
        call.process = static_cast<runtime::simir::ProcessId>(index);
        call.instruction = 7U;
        call.kind = SdfVitalCallKind::Delay;
        call.source = { "fixture.vhd",
            static_cast<std::uint32_t>(10U + index), 3U };
        call.canonical_identity = "vital-live-delay-"
            + std::to_string(index);
        SdfVitalPathTimingRecord path;
        path.node_id = index + 1U;
        path.cell_id = index + 1U;
        path.construct_kind = frontend::SdfConstructKind::Iopath;
        path.instance_path = instances[index];
        path.call = call;
        path.endpoint_signals = {
            static_cast<runtime::simir::SignalId>(index + 4U)
        };
        path.before_delay_ticks = { 5U };
        path.canonical_identity = identity + "-path-"
            + std::to_string(index);
        SdfVitalModelRecord model;
        model.node_id = path.node_id;
        model.cell_id = path.cell_id;
        model.instance_path = path.instance_path;
        model.owned_processes = { call.process };
        model.call = call;
        model.target_identity = identity + "-target-"
            + std::to_string(index);
        model.path_identity = path.canonical_identity;
        model.canonical_identity = identity + "-model-"
            + std::to_string(index);
        SdfVitalScheduledDelay scheduled;
        scheduled.call = call;
        scheduled.kind = runtime::simir::VitalDelayKind::path;
        scheduled.shape = runtime::simir::VitalDelayShape::single;
        scheduled.mode = runtime::simir::VitalGlitchMode::transport;
        scheduled.endpoint_signals = path.endpoint_signals;
        scheduled.source_delay_ticks = { 5U };
        scheduled.effective_delay_ticks = { delay_values[index] };
        scheduled.annotated = delay_values[index] != 5U;
        scheduled.canonical_identity = identity + "-delay-"
            + std::to_string(index);
        paths.push_back(std::move(path));
        models.push_back(std::move(model));
        delays.push_back(std::move(scheduled));
    }
    SdfVitalCallReference check_call;
    check_call.process = 4U;
    check_call.instruction = 0U;
    check_call.kind = SdfVitalCallKind::TimingCheck;
    check_call.source = { "fixture.vhd", 30U, 3U };
    check_call.canonical_identity = "vital-live-check";
    SdfVitalPathTimingRecord check_path;
    check_path.node_id = 5U;
    check_path.cell_id = 5U;
    check_path.construct_kind = frontend::SdfConstructKind::Period;
    check_path.instance_path = instances[4];
    check_path.call = check_call;
    check_path.endpoint_signals = { 8U };
    check_path.before_check_ticks = { 10, 0, 0, 0 };
    check_path.canonical_identity = identity + "-check-path";
    SdfVitalModelRecord check_model;
    check_model.node_id = 5U;
    check_model.cell_id = 5U;
    check_model.instance_path = check_path.instance_path;
    check_model.owned_processes = { 4U };
    check_model.call = check_call;
    check_model.target_identity = identity + "-check-target";
    check_model.path_identity = check_path.canonical_identity;
    check_model.canonical_identity = identity + "-check-model";
    paths.push_back(std::move(check_path));
    models.push_back(std::move(check_model));
    SdfValuePolicy value_policy;
    auto path_plan = std::make_shared<const SdfVitalPathTimingPlan>(
        nullptr, value_policy, std::move(paths), identity + "-paths");
    auto model_plan = std::make_shared<const SdfVitalModelPlan>(path_plan,
        std::vector<SdfVitalWrapperRegistration> { }, std::move(models),
        identity + "-models");
    std::vector<SdfVitalTimingGenericValue> generics;
    if (with_generics) {
        generics.push_back({ "vital-live-delay-0", 0U,
            delay_values[0], std::nullopt, identity + "-generic-delay" });
        generics.push_back({ "vital-live-check", 0U, std::nullopt,
            static_cast<std::int64_t>(check_value),
            identity + "-generic-check" });
    }
    auto precedence
        = std::make_shared<const SdfVitalPrecedenceApplication>(model_plan,
            std::vector<std::shared_ptr<const SdfVitalModelPlan>> { },
            std::move(generics),
            std::vector<SdfVitalAnnotationControl> { },
            SdfVitalPrecedencePolicy { },
            std::vector<SdfVitalEffectiveTimingValue> { },
            identity + "-precedence");
    auto scheduling
        = std::make_shared<const SdfVitalSchedulingApplication>(precedence,
            design, std::move(delays), identity + "-scheduling");
    SdfVitalScheduledTimingCheck scheduled_check;
    scheduled_check.call = check_call;
    scheduled_check.kind
        = runtime::simir::VitalTimingCheckKind::period_pulse;
    scheduled_check.endpoint_signals = { 8U };
    scheduled_check.source_limits = { 10U, 0U, 0U, 0U };
    scheduled_check.effective_limits = { check_value, 0U, 0U, 0U };
    scheduled_check.message = "live VITAL period violation";
    scheduled_check.violation_source = check_call.source;
    scheduled_check.canonical_identity = identity + "-check";
    return std::make_shared<const SdfVitalTimingCheckApplication>(
        scheduling, design,
        std::vector<SdfVitalScheduledTimingCheck> {
            std::move(scheduled_check) },
        identity);
}

fsim::app::SdfVitalReannotationLayer layer(const std::uint64_t file,
    const std::uint64_t cell, std::string file_identity,
    std::string root, std::string pattern,
    std::shared_ptr<const fsim::app::SdfVitalTimingCheckApplication> timing)
{
    return { file, cell, std::move(file_identity), std::move(root),
        std::move(pattern), std::move(timing) };
}

void require_diagnostic(const auto& result, const std::string_view code)
{
    require(!result.ok()
            && std::ranges::any_of(result.diagnostics,
                [&](const auto& diagnostic) {
                    return diagnostic.code == code;
                }),
        "expected diagnostic " + std::string { code });
}

void test_ordering_wildcards_generics_and_rollback()
{
    using namespace fsim::app;
    const auto design = make_design();
    const auto baseline
        = make_application(design, { 5U, 5U, 5U, 5U }, 10U, "base");
    const auto alpha
        = make_application(design, { 3U, 3U, 30U, 30U }, 4U, "alpha");
    const auto override = make_application(
        design, { 1U, 31U, 31U, 31U }, 6U, "override", true);
    const auto beta
        = make_application(design, { 70U, 70U, 7U, 7U }, 40U, "beta");
    std::vector<SdfVitalReannotationLayer> layers {
        layer(1U, 0U, "alpha.sdf", "alpha", "u*", alpha),
        layer(2U, 0U, "override.sdf", "alpha", "u0", override),
        layer(1U, 0U, "beta.sdf", "beta", "*", beta),
    };
    const auto result = apply_sdf_vital_reannotation(
        baseline, layers, 8U);
    require(result.ok(), "ordered VITAL reannotation must publish");
    require(result.application->delays()[0].effective_delay_ticks[0] == 1U
            && result.application->delays()[1]
                    .effective_delay_ticks[0]
                == 3U
            && result.application->delays()[2]
                    .effective_delay_ticks[0]
                == 7U
            && result.application->delays()[3]
                    .effective_delay_ticks[0]
                == 7U
            && result.application->checks()[0].effective_limits[0] == 4U,
        "file, cell, wildcard and root precedence must be exact");
    require(result.application->generics().size() == 1U
            && result.application->generics()[0].generic_identity
                == "override-generic-delay",
        "winning generic provenance must replace only its selected call");
    const auto identity = result.application->semantic_identity();
    std::ranges::reverse(layers);
    const auto reversed = apply_sdf_vital_reannotation(
        baseline, layers, 8U);
    require(reversed.ok()
            && reversed.application->semantic_identity() == identity,
        "explicit precedence must make layer order deterministic");

    const std::array duplicate_layers {
        layer(1U, 0U, "a.sdf", "alpha", "u0", alpha),
        layer(1U, 0U, "b.sdf", "alpha", "u0", alpha),
    };
    require_diagnostic(apply_sdf_vital_reannotation(
                           baseline, duplicate_layers, 9U),
        "FSIM-SDF-VITAL-REANNOTATION-002");
    require(baseline->scheduling()->delays()[0].effective_delay_ticks[0]
            == 5U,
        "rejected duplicate transaction must not mutate its baseline");
}

std::size_t run_live_case(
    const fsim::app::SdfVitalTimingStatePolicy state_policy,
    const bool expect_pending_rejection)
{
    using namespace fsim;
    using namespace app;
    using namespace runtime;
    const auto design = make_design();
    const auto baseline
        = make_application(design, { 5U, 5U, 5U, 5U }, 10U, "live-base");
    const auto replacement
        = make_application(design, { 1U, 3U, 7U, 7U }, 7U, "live-new");
    const std::array layers {
        layer(1U, 0U, "live.sdf", "alpha", "u*", replacement)
    };
    const auto policy = expect_pending_rejection
        ? SdfVitalPendingTransactionPolicy::RejectIfPending
        : SdfVitalPendingTransactionPolicy::PreserveScheduledTiming;
    const auto transaction = apply_sdf_vital_reannotation(
        baseline, layers, 11U, policy, state_policy);
    require(transaction.ok(), "live VITAL transaction must publish");
    auto interpreter = baseline->design().create_interpreter();
    std::vector<std::pair<SimulationTick, runtime::simir::SignalId>> changes;
    interpreter->set_signal_change_hook([&](const auto signal,
                                            const auto& value,
                                            const auto time) {
        if ((signal == 4U || signal == 5U)
            && value.to_msb_string() == "1") {
            changes.emplace_back(time, signal);
        }
    });
    std::size_t reports { };
    interpreter->set_report_hook([&](auto, const std::string_view message,
                                     auto, const auto&, const auto time,
                                     auto) {
        if (message == "live VITAL period violation" && time >= 6U)
            ++reports;
    });
    interpreter->start();
    require_diagnostic(commit_sdf_vital_reannotation(
                           *transaction.application, *interpreter),
        "FSIM-SDF-VITAL-REANNOTATION-001");
    bool committed { };
    interpreter->scheduler().set_safe_point_hook(
        [&](auto& scheduler, const auto phase) {
            if (!committed && scheduler.now() == 1U
                && phase == runtime::SchedulerPhase::postponed) {
                const auto first = commit_sdf_vital_reannotation(
                    *transaction.application, *interpreter);
                if (expect_pending_rejection) {
                    require_diagnostic(first,
                        "FSIM-SDF-VITAL-REANNOTATION-001");
                } else {
                    const auto second = commit_sdf_vital_reannotation(
                        *transaction.application, *interpreter);
                    require(first.ok() && second.ok()
                            && first.generation == 11U
                            && second.generation == 11U,
                        "same-safe-point observers must see one generation");
                }
                committed = true;
            }
        });
    interpreter->schedule_signal_at(0U,
        PackedLogic4::from_logic9_msb_string("1"), 0U);
    interpreter->schedule_signal_at(9U,
        PackedLogic4::from_logic9_msb_string("1"), 1U);
    interpreter->schedule_signal_at(1U,
        PackedLogic4::from_logic9_msb_string("1"), 3U);
    interpreter->schedule_signal_at(8U,
        PackedLogic4::from_logic9_msb_string("1"), 0U);
    interpreter->schedule_signal_at(8U,
        PackedLogic4::from_logic9_msb_string("0"), 3U);
    interpreter->schedule_signal_at(8U,
        PackedLogic4::from_logic9_msb_string("1"), 6U);
    const auto run = interpreter->run(8U);
    require(run.status != runtime::RunStatus::stopped && committed,
        "live VITAL reannotation run must reach its safe point");
    if (!expect_pending_rejection) {
        require(std::ranges::find(changes,
                    std::pair<SimulationTick,
                        runtime::simir::SignalId> { 5U, 4U })
                    != changes.end()
                && std::ranges::find(changes,
                       std::pair<SimulationTick,
                           runtime::simir::SignalId> { 6U, 5U })
                    != changes.end(),
            "pending writes must retain old timing while future writes use new timing");
    }
    return reports;
}

void test_safe_point_pending_and_timing_state()
{
    using namespace fsim::app;
    require(run_live_case(SdfVitalTimingStatePolicy::PreserveHistory,
                false)
            == 1U,
        "preserved VITAL timing history must detect the post-commit period");
    require(run_live_case(SdfVitalTimingStatePolicy::ResetHistory, false)
            == 0U,
        "reset VITAL timing history must establish a new first observation");
    (void)run_live_case(
        SdfVitalTimingStatePolicy::PreserveHistory, true);
}
} // namespace

int main()
{
    try {
        try {
            test_ordering_wildcards_generics_and_rollback();
        } catch (const std::exception& error) {
            throw std::runtime_error {
                std::string { "ordering: " } + error.what()
            };
        }
        try {
            test_safe_point_pending_and_timing_state();
        } catch (const std::exception& error) {
            throw std::runtime_error {
                std::string { "live: " } + error.what()
            };
        }
        std::cout << "sdf vital reannotation tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "sdf vital reannotation tests failed: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}

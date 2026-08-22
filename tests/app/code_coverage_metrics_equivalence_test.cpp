// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_internal.hpp"

#include "fsim/runtime/coverage_condition_evaluation.hpp"
#include "fsim/runtime/coverage_condition_outcomes.hpp"
#include "fsim/runtime/coverage_expression.hpp"
#include "fsim/runtime/coverage_fsm.hpp"
#include "fsim/runtime/coverage_toggle.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fsim;

constexpr std::size_t kInstanceCount = 6U;
constexpr std::size_t kEventsPerInstance = 4U;

runtime::CodeCoveragePointId point(
    const std::size_t instance, const std::size_t ordinal)
{
    return { 0x1791900000000000ULL
            + static_cast<std::uint64_t>(instance + 1U),
        static_cast<std::uint64_t>(ordinal + 1U) };
}

struct Snapshot {
    std::vector<std::uint64_t> events;
    std::vector<std::uint64_t> exact;

    friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

void append_point(
    std::vector<std::uint64_t>& output,
    const runtime::CodeCoveragePointId id)
{
    output.push_back(id.high);
    output.push_back(id.low);
}

void append_metric_snapshot(
    Snapshot& snapshot, const std::size_t instance)
{
    using enum runtime::CoverageConditionLogicalOperator;
    using enum runtime::CoverageConditionOperand;
    using runtime::CoverageConditionTruth;

    const std::array left_path {
        runtime::CoverageConditionPathStep { And, Left }
    };
    const std::array right_path {
        runtime::CoverageConditionPathStep { And, Right }
    };
    const std::array atoms {
        runtime::CoverageConditionEvaluationAtom {
            point(instance, 10U), 0U, left_path },
        runtime::CoverageConditionEvaluationAtom {
            point(instance, 11U), 1U, right_path },
    };
    const bool generated_even = (instance % 2U) == 0U;
    const auto evaluation = runtime::evaluate_coverage_condition(
        atoms, [&](const auto& atom) {
            if (atom.condition_index == 0U) {
                return std::optional { generated_even
                        ? CoverageConditionTruth::True
                        : CoverageConditionTruth::False };
            }
            return std::optional { CoverageConditionTruth::False };
        });
    assert(evaluation.ok());

    std::array outcomes {
        runtime::CoverageConditionOutcome { atoms[0].point },
        runtime::CoverageConditionOutcome { atoms[1].point },
    };
    auto recorded = runtime::record_coverage_condition_outcomes(
        outcomes, evaluation.observations);
    assert(recorded.ok());
    const std::array unknown {
        runtime::CoverageConditionObservation {
            atoms[0].point, 0U, CoverageConditionTruth::Unknown }
    };
    recorded = runtime::record_coverage_condition_outcomes(
        outcomes, unknown);
    assert(recorded.ok());
    snapshot.exact.insert(snapshot.exact.end(), {
                                                    static_cast<std::uint64_t>(evaluation.decision_truth),
                                                    static_cast<std::uint64_t>(evaluation.observations.size()),
                                                    static_cast<std::uint64_t>(evaluation.skipped.size()),
                                                });
    for (const auto& outcome : outcomes) {
        append_point(snapshot.exact, outcome.point);
        snapshot.exact.insert(snapshot.exact.end(), {
                                                        outcome.true_hits,
                                                        outcome.false_hits,
                                                        outcome.unknown_observations,
                                                        static_cast<std::uint64_t>(runtime::coverage_condition_outcome_status(outcome)),
                                                    });
    }

    const std::array expression_atoms { atoms[0].point, atoms[1].point };
    const auto expression
        = runtime::build_coverage_expression_inventory(expression_atoms);
    assert(expression.ok() && expression.inventory.combinations.size() == 4U
        && expression.inventory.omission.omitted_combinations == 0U);
    snapshot.exact.push_back(expression.inventory.combinations.size());
    for (const auto& combination : expression.inventory.combinations) {
        snapshot.exact.insert(snapshot.exact.end(), {
                                                        combination.ordinal,
                                                        static_cast<std::uint64_t>(runtime::coverage_expression_combination_truth(combination, 0U)),
                                                        static_cast<std::uint64_t>(runtime::coverage_expression_combination_truth(combination, 1U)),
                                                    });
    }

    std::array toggle_outcomes {
        runtime::CoverageToggleOutcome { point(instance, 20U), 0U }
    };
    const std::array binary_transitions {
        runtime::CoverageToggleValueTransition { point(instance, 20U), 0U,
            0U, runtime::CoverageToggleLogicValue::Zero,
            generated_even ? runtime::CoverageToggleLogicValue::One
                           : runtime::CoverageToggleLogicValue::Unknown },
        runtime::CoverageToggleValueTransition { point(instance, 20U), 0U,
            0U, generated_even ? runtime::CoverageToggleLogicValue::One : runtime::CoverageToggleLogicValue::Unknown,
            generated_even ? runtime::CoverageToggleLogicValue::Zero
                           : runtime::CoverageToggleLogicValue::HighImpedance },
    };
    const auto toggled = runtime::record_coverage_toggle_value_transitions(
        toggle_outcomes, binary_transitions);
    assert(toggled.ok());
    const auto& toggle = toggle_outcomes.front();
    append_point(snapshot.exact, toggle.point);
    snapshot.exact.insert(snapshot.exact.end(), {
                                                    toggle.zero_to_one_hits,
                                                    toggle.one_to_zero_hits,
                                                    toggle.unknown_transition_observations,
                                                    toggle.high_impedance_transition_observations,
                                                    static_cast<std::uint64_t>(runtime::coverage_toggle_status(toggle)),
                                                });

    runtime::CoverageFsmMachineDefinition machine;
    machine.current_state_object = point(instance, 30U);
    machine.states = { point(instance, 31U), point(instance, 32U) };
    machine.legal_transitions = {
        { point(instance, 31U), point(instance, 32U) },
        { point(instance, 32U), point(instance, 31U) },
    };
    runtime::CoverageFsmDefinition definition {
        point(instance, 29U),
        static_cast<std::uint32_t>(instance),
        "top.generated[" + std::to_string(instance) + "]",
        { std::move(machine) },
    };
    auto built = runtime::make_coverage_fsm_runtime_model(definition);
    assert(built.ok());
    auto& model = *built.model;
    const std::array complete_observations {
        runtime::CoverageFsmObservation {
            point(instance, 30U), point(instance, 31U) },
        runtime::CoverageFsmObservation {
            point(instance, 30U), point(instance, 32U) },
        runtime::CoverageFsmObservation {
            point(instance, 30U), point(instance, 31U) },
    };
    const std::array partial_observations {
        runtime::CoverageFsmObservation {
            point(instance, 30U), point(instance, 31U) },
    };
    const auto fsm_recorded = runtime::record_coverage_fsm_observations(
        model, generated_even ? std::span<const runtime::CoverageFsmObservation> { complete_observations } : std::span<const runtime::CoverageFsmObservation> { partial_observations });
    assert(fsm_recorded.ok());
    const auto summary = runtime::summarize_coverage_fsm(model);
    assert(summary.ok());
    append_point(snapshot.exact, model.instance_identity);
    snapshot.exact.insert(snapshot.exact.end(), {
                                                    summary.summary->state_visits.total,
                                                    summary.summary->state_visits.covered,
                                                    summary.summary->legal_transitions.total,
                                                    summary.summary->legal_transitions.covered,
                                                });
    for (const auto& visit : model.state_visits) {
        append_point(snapshot.exact, visit.id);
        snapshot.exact.push_back(visit.hits);
    }
    for (const auto& transition : model.legal_transitions) {
        append_point(snapshot.exact, transition.id);
        snapshot.exact.push_back(transition.hits);
    }
}

Snapshot capture(
    const std::optional<project::Optimization> optimization,
    const bool debug_engine = false)
{
    runtime::simir::Interpreter interpreter;
    interpreter.set_code_coverage_counters(std::vector<std::uint64_t>(
        kInstanceCount * kEventsPerInstance));
    std::unique_ptr<compiler::LlvmJit> jit;
    if (optimization) {
        compiler::LlvmJitOptions options;
        options.optimization
            = app::application_detail::jit_optimization(*optimization);
        options.debug_instrumentation = debug_engine;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
    }

    constexpr std::array standards {
        std::string_view { "2005" },
        std::string_view { "2005" },
        std::string_view { "2017" },
        std::string_view { "2017" },
        std::string_view { "2008" },
        std::string_view { "2008" },
    };
    constexpr std::array profiles {
        std::string_view { "none" },
        std::string_view { "none" },
        std::string_view { "none" },
        std::string_view { "none" },
        std::string_view { "fsim-vhdl-standard" },
        std::string_view { "fsim-vhdl-standard" },
    };
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    std::size_t debug_points { };
    interpreter.set_execution_point_hook(
        [&](runtime::Scheduler&,
            const runtime::simir::ExecutionPoint&) { ++debug_points; });

    for (std::size_t instance = 0U; instance < kInstanceCount; ++instance) {
        runtime::simir::Process process;
        process.id = static_cast<runtime::simir::ProcessId>(instance);
        process.name = "coverage_metrics_generated_"
            + std::to_string(instance);
        process.language_standard = std::string { standards[instance] };
        process.compatibility_profile = std::string { profiles[instance] };
        process.operations.push_back(runtime::simir::DebugPoint {
            runtime::simir::DebugPointKind::statement,
            { instance < 2U         ? "rtl/verilog_leaf.v"
                    : instance < 4U ? "rtl/systemverilog_leaf.sv"
                                    : "rtl/vhdl_leaf.vhd",
                static_cast<std::uint32_t>(instance + 1U), 1U },
            "top.generated[" + std::to_string(instance) + "]",
        });
        for (std::size_t event = 0U; event < kEventsPerInstance; ++event) {
            process.operations.push_back(runtime::simir::CodeCoverageHit {
                point(instance, event),
                runtime::CodeCoverageMetric::Statement,
                { static_cast<std::uint32_t>(
                    instance * kEventsPerInstance + event) },
            });
        }
        process.operations.push_back(runtime::simir::Halt { });

        const auto symbol = "coverage_metrics_generated_"
            + std::to_string(instance);
        std::optional<compiler::JitProcessHandle> handle;
        if (jit) {
            jit->add_process(symbol, process, no_signal_widths);
            handle = jit->lookup(symbol);
        }
        const auto process_id = interpreter.add_process(std::move(process));
        if (jit) {
            interpreter.set_process_executor(
                process_id,
                std::make_unique<app::application_detail::LlvmProcessExecutor>(
                    *jit, *handle, interpreter.process_program(process_id),
                    no_signal_widths,
                    std::span<const runtime::simir::ValueKind> { },
                    std::span<const runtime::simir::ResolutionKind> { }));
        }
    }
    const auto execution = interpreter.run();
    assert(execution.status == runtime::RunStatus::completed);
    assert(debug_points >= kInstanceCount);

    Snapshot snapshot;
    snapshot.events.assign(interpreter.code_coverage_counters().begin(),
        interpreter.code_coverage_counters().end());
    for (const auto hits : snapshot.events) {
        assert(hits == 1U);
    }
    for (std::size_t instance = 0U; instance < kInstanceCount; ++instance) {
        append_metric_snapshot(snapshot, instance);
    }
    return snapshot;
}

} // namespace

int main()
{
    const auto reference = capture(std::nullopt);
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o1,
             fsim::project::Optimization::o2,
             fsim::project::Optimization::o3,
         }) {
        assert(capture(optimization) == reference);
    }
    assert(capture(fsim::project::Optimization::o0, true) == reference);
}

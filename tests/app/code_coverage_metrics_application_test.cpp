// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/verilog_coverage_conditions.hpp"
#include "fsim/elaboration/verilog_toggle_inventory.hpp"
#include "fsim/elaboration/vhdl_coverage_conditions.hpp"
#include "fsim/elaboration/vhdl_toggle_inventory.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/runtime/coverage_condition_outcomes.hpp"
#include "fsim/runtime/coverage_expression.hpp"
#include "fsim/runtime/coverage_fsm.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace fsim;

constexpr std::string_view kSystemVerilogSource = R"(module sv_leaf(
  input logic a, input logic b, output logic q);
  logic state;
  always_comb begin
    if (a && b) q = state;
    else q = 1'b0;
  end
endmodule
)";

constexpr std::string_view kVhdlSource = R"(entity vhdl_leaf is
  port (a : in boolean; b : in boolean; q : out boolean);
end vhdl_leaf;
architecture rtl of vhdl_leaf is
  signal state : boolean;
begin
  worker : process(a, b, state)
  begin
    if a and b then q <= state; else q <= false; end if;
  end process;
end rtl;
)";

std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

struct InputSource {
    elaboration::CoverageConditionSource condition;
    elaboration::VerilogCoverageSource toggle;
};

InputSource source(
    const std::string_view name, const std::string_view contents)
{
    const auto root = std::filesystem::path { "/coverage-metrics" };
    auto identity = frontend::make_code_coverage_source_identity(
        root, root / name, bytes(contents));
    assert(identity.ok());
    auto source_name = (root / name).generic_string();
    return {
        { source_name, *identity.identity },
        { std::move(source_name), std::move(*identity.identity) },
    };
}

elaboration::CoverageInventoryOwner owner(
    const InputSource& input,
    const std::string_view hierarchy, const frontend::Language language,
    const std::uint32_t specialization)
{
    return { specialization, hierarchy, language,
        input.condition.source_name, { },
        "work", language == frontend::Language::Vhdl2008 ? "vhdl:work.vhdl_leaf(rtl)" : "sv:work.sv_leaf",
        { } };
}

std::vector<runtime::CoverageConditionEvaluationAtom> atoms(
    const std::span<const elaboration::CoverageConditionPoint> points)
{
    std::vector<runtime::CoverageConditionEvaluationAtom> result;
    for (const auto& point : points) {
        if (point.decision_index == 0U) {
            result.push_back(
                { point.id, point.condition_index, point.evaluation_path });
        }
    }
    return result;
}

std::vector<runtime::CoverageConditionOutcome> condition_outcomes(
    const std::span<const runtime::CoverageConditionEvaluationAtom> input,
    const bool first_truth)
{
    std::vector<runtime::CoverageConditionOutcome> outcomes;
    for (const auto& atom : input) {
        outcomes.push_back({ atom.point });
    }
    const auto evaluated = runtime::evaluate_coverage_condition(
        input, [&](const auto& atom) {
            return std::optional { atom.condition_index == 0U
                    ? first_truth ? runtime::CoverageConditionTruth::True
                                  : runtime::CoverageConditionTruth::False
                    : runtime::CoverageConditionTruth::True };
        });
    assert(evaluated.ok());
    const auto recorded = runtime::record_coverage_condition_outcomes(
        outcomes, evaluated.observations);
    assert(recorded.ok());
    return outcomes;
}

template <typename ToggleInventory>
void prove_instance_and_source_aggregation(
    ToggleInventory first, ToggleInventory second,
    const std::span<const elaboration::CoverageConditionPoint> conditions)
{
    assert(first.instance_identity != second.instance_identity);
    assert(!first.objects.empty() && first.objects.size() == second.objects.size());
    assert(first.objects.front().source_point
        == second.objects.front().source_point);
    assert(first.objects.front().point != second.objects.front().point);

    auto first_toggle = std::array { first.outcomes.front() };
    auto second_toggle = std::array { second.outcomes.front() };
    const std::array rising { runtime::CoverageToggleTransition {
        first_toggle[0].point, first_toggle[0].bit_index, 0U, false, true } };
    const std::array falling { runtime::CoverageToggleTransition {
        second_toggle[0].point, second_toggle[0].bit_index, 0U, true, false } };
    assert(runtime::record_coverage_toggle_transitions(
        first_toggle, rising)
            .ok());
    assert(runtime::record_coverage_toggle_transitions(
        second_toggle, falling)
            .ok());
    assert(runtime::coverage_toggle_status(first_toggle[0])
            == runtime::CodeCoverageStatus::Partial
        && runtime::coverage_toggle_status(second_toggle[0])
            == runtime::CodeCoverageStatus::Partial);
    runtime::CoverageToggleOutcome source_union {
        first.objects.front().source_point,
        first_toggle[0].bit_index,
        first_toggle[0].zero_to_one_hits + second_toggle[0].zero_to_one_hits,
        first_toggle[0].one_to_zero_hits + second_toggle[0].one_to_zero_hits,
    };
    assert(runtime::coverage_toggle_status(source_union)
        == runtime::CodeCoverageStatus::Covered);

    const auto evaluation_atoms = atoms(conditions);
    assert(evaluation_atoms.size() == 2U);
    const auto first_conditions
        = condition_outcomes(evaluation_atoms, true);
    const auto second_conditions
        = condition_outcomes(evaluation_atoms, false);
    assert(first_conditions[0].true_hits == 1U
        && second_conditions[0].false_hits == 1U
        && second_conditions[1].true_hits == 0U);
    assert(runtime::coverage_condition_outcome_status(first_conditions[0])
            == runtime::CodeCoverageStatus::Partial
        && runtime::coverage_condition_outcome_status(second_conditions[0])
            == runtime::CodeCoverageStatus::Partial);
    auto condition_union = first_conditions[0];
    condition_union.true_hits += second_conditions[0].true_hits;
    condition_union.false_hits += second_conditions[0].false_hits;
    assert(runtime::coverage_condition_outcome_status(condition_union)
        == runtime::CodeCoverageStatus::Covered);

    const std::array expression_points {
        evaluation_atoms[0].point, evaluation_atoms[1].point
    };
    const auto expression
        = runtime::build_coverage_expression_inventory(expression_points);
    assert(expression.ok() && expression.inventory.combinations.size() == 4U
        && expression.inventory.omission.omitted_combinations == 0U);

    const auto first_instance = runtime::CodeCoveragePointId {
        first.instance_identity.high, first.instance_identity.low
    };
    const auto second_instance = runtime::CodeCoveragePointId {
        second.instance_identity.high, second.instance_identity.low
    };
    const auto make_definition = [&](const auto instance,
                                     const std::string_view path) {
        runtime::CoverageFsmMachineDefinition machine;
        machine.current_state_object = evaluation_atoms[0].point;
        machine.states.assign(
            expression_points.begin(), expression_points.end());
        machine.legal_transitions = {
            { expression_points[0], expression_points[1] },
            { expression_points[1], expression_points[0] },
        };
        return runtime::CoverageFsmDefinition {
            instance, 0U, std::string { path }, { std::move(machine) }
        };
    };
    auto first_model = runtime::make_coverage_fsm_runtime_model(
        make_definition(first_instance, first.instance));
    auto second_model = runtime::make_coverage_fsm_runtime_model(
        make_definition(second_instance, second.instance));
    assert(first_model.ok() && second_model.ok());
    assert(first_model.model->state_visits.front().id
        != second_model.model->state_visits.front().id);
    const std::array forward {
        runtime::CoverageFsmObservation {
            evaluation_atoms[0].point, expression_points[0] },
        runtime::CoverageFsmObservation {
            evaluation_atoms[0].point, expression_points[1] },
    };
    const std::array reverse {
        runtime::CoverageFsmObservation {
            evaluation_atoms[0].point, expression_points[1] },
        runtime::CoverageFsmObservation {
            evaluation_atoms[0].point, expression_points[0] },
    };
    assert(runtime::record_coverage_fsm_observations(
        *first_model.model, forward)
            .ok());
    assert(runtime::record_coverage_fsm_observations(
        *second_model.model, reverse)
            .ok());
    const auto first_summary
        = runtime::summarize_coverage_fsm(*first_model.model);
    const auto second_summary
        = runtime::summarize_coverage_fsm(*second_model.model);
    assert(first_summary.ok() && second_summary.ok());
    assert(first_summary.summary->state_visits.covered == 2U
        && second_summary.summary->state_visits.covered == 2U
        && first_summary.summary->legal_transitions.covered == 1U
        && second_summary.summary->legal_transitions.covered == 1U);
}

void exercise_systemverilog()
{
    auto input = source("rtl/sv_leaf.sv", kSystemVerilogSource);
    const auto parsed = frontend::parse_text(input.condition.source_name,
        kSystemVerilogSource, frontend::Language::SystemVerilog2017);
    assert(parsed.ok() && parsed.design.units.size() == 1U
        && parsed.design.units.front().processes.size() == 1U);
    const auto& unit = parsed.design.units.front();
    const auto conditions = elaboration::discover_verilog_coverage_conditions(
        unit.processes.front().statements,
        frontend::Language::SystemVerilog2017,
        std::span { &input.condition, 1U });
    assert(conditions.ok() && conditions.points.size() == 2U);
    const auto first = elaboration::make_verilog_toggle_inventory(unit,
        owner(input, "top.sv_gen[0]",
            frontend::Language::SystemVerilog2017, 0U),
        std::span { &input.toggle, 1U });
    const auto second = elaboration::make_verilog_toggle_inventory(unit,
        owner(input, "top.sv_gen[1]",
            frontend::Language::SystemVerilog2017, 1U),
        std::span { &input.toggle, 1U });
    assert(first.ok() && second.ok());
    prove_instance_and_source_aggregation(
        *first.inventory, *second.inventory, conditions.points);
}

void exercise_vhdl()
{
    auto input = source("rtl/vhdl_leaf.vhd", kVhdlSource);
    const auto parsed = frontend::parse_text(
        input.condition.source_name, kVhdlSource,
        frontend::Language::Vhdl2008, frontend::VhdlStandard::Vhdl2008);
    assert(parsed.ok() && parsed.design.units.size() == 2U
        && parsed.design.units.back().processes.size() == 1U);
    const auto& entity = parsed.design.units.front();
    const auto& architecture = parsed.design.units.back();
    const auto conditions = elaboration::discover_vhdl_coverage_conditions(
        architecture.processes.front().statements,
        frontend::Language::Vhdl2008, frontend::VhdlStandard::Vhdl2008,
        std::span { &input.condition, 1U });
    assert(conditions.ok() && conditions.points.size() == 2U);
    const auto first = elaboration::make_vhdl_toggle_inventory(architecture,
        entity.ports,
        owner(input, "top.vhdl_gen[0]", frontend::Language::Vhdl2008, 0U),
        std::span { &input.toggle, 1U });
    const auto second = elaboration::make_vhdl_toggle_inventory(architecture,
        entity.ports,
        owner(input, "top.vhdl_gen[1]", frontend::Language::Vhdl2008, 1U),
        std::span { &input.toggle, 1U });
    assert(first.ok() && second.ok());
    prove_instance_and_source_aggregation(
        *first.inventory, *second.inventory, conditions.points);
}

} // namespace

int main()
{
    exercise_systemverilog();
    exercise_vhdl();
}

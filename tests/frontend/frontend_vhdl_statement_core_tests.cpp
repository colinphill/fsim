// SPDX-License-Identifier: Apache-2.0

#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string { message });
    }
}

} // namespace

void test_vhdl_selected_assignments()
{
    const auto result = parse_text(
        "selected_assignment.vhd",
        R"(
entity selected_assignment is
end entity;

architecture rtl of selected_assignment is
  signal selector : std_logic_vector(1 downto 0);
  signal a : std_logic_vector(3 downto 0);
  signal b : std_logic_vector(3 downto 0);
  signal result : std_logic_vector(3 downto 0);
begin
  choose: with selector select
    result <=
      a after 2 ns when "00" | "01",
      b when others;
end architecture;
)",
        Language::Vhdl2008);
    require(result.ok(), "VHDL selected signal assignments must parse");
    const auto* architecture = result.design.find(UnitKind::VhdlArchitecture, "rtl");
    require(
        architecture
            && architecture->concurrent_statements.size() == 1,
        "a selected signal assignment must be retained as one statement");
    const auto& selected = architecture->concurrent_statements.front();
    require(
        selected.kind == StatementKind::Case
            && selected.label == "choose"
            && selected.condition.text == "selector"
            && selected.case_alternatives.size() == 2
            && selected.case_alternatives[0].choices.size() == 2
            && selected.case_alternatives[0].statements.size() == 1
            && selected.case_alternatives[0]
                .statements.front()
                .delay
                .has_value()
            && selected.case_alternatives[0]
                    .statements.front()
                    .delay->magnitude
                == 2
            && selected.case_alternatives[1].is_default,
        "selected assignment choices, waveform delay, default, and label");

    const auto missing_others = parse_text(
        "selected_missing_others.vhd",
        R"(
entity selected_missing_others is
end entity;
architecture rtl of selected_missing_others is
  signal selector : std_logic;
  signal result : std_logic;
begin
  with selector select result <= '0' when '0';
end architecture;
)",
        Language::Vhdl2008);
    require(
        !missing_others.ok()
            && std::ranges::any_of(
                missing_others.diagnostics,
                [](const auto& diagnostic) {
                    return diagnostic.code
                        == "FSIM-VHDL-SEM-029";
                }),
        "bounded selected assignments without others must be diagnosed");

    const auto invalid_others = parse_text(
        "selected_invalid_others.vhd",
        R"(
entity selected_invalid_others is
end entity;
architecture rtl of selected_invalid_others is
  signal selector : std_logic;
  signal result : std_logic;
begin
  with selector select
    result <= '0' when others, '1' when '1', '0' when others;
end architecture;
)",
        Language::Vhdl2008);
    require(
        !invalid_others.ok()
            && std::ranges::any_of(
                invalid_others.diagnostics,
                [](const auto& diagnostic) {
                    return diagnostic.code
                        == "FSIM-VHDL-SEM-027";
                })
            && std::ranges::any_of(
                invalid_others.diagnostics,
                [](const auto& diagnostic) {
                    return diagnostic.code
                        == "FSIM-VHDL-SEM-028";
                }),
        "duplicate and nonfinal selected-assignment others alternatives "
        "must be diagnosed");

    const auto inventory = parse_text(
        "statement_inventory.vhd",
        R"(
architecture rtl of statement_inventory is
  signal selector : std_logic;
  signal source : std_logic;
  signal result : std_logic;
begin
  direct_write: result <= source;
  direct_call: tick(result);
  postponed_assert: postponed assert result = result;
  postponed_call: postponed tick(result);
  generated: if true generate
    generated_select: with selector select
      result <= source when '1', '0' when others;
    generated_call: tick(result);
    generated_postponed_assert: postponed assert result = result;
    generated_postponed_call: postponed tick(result);
  end generate generated;
  reactive: process(all)
  begin
    result <= source;
  end process reactive;
  worker: process
    variable local_value : std_logic;
  begin
    sequential_select: with selector select
      result <= source when '1', '0' when others;
    variable_select: with selector select
      local_value := source when '1', '0' when others;
    branch: if selector = '1' then
      branch_null: null;
    end if branch;
    choice: case selector is
      when '1' => selected_null: null;
      when others => fallback_null: null;
    end case choice;
    suspended: wait;
  end process worker;
  observer: postponed process(result)
  begin
    null;
  end postponed process observer;
end architecture;
)",
        Language::Vhdl2008);
    require(
        inventory.ok(),
        "labeled sequential/concurrent statement inventory must parse");
    const auto& inventory_unit = inventory.design.units.front();
    require(
        inventory_unit.concurrent_statements.size() == 4
            && inventory_unit.concurrent_statements[0].label
                == "direct_write"
            && inventory_unit.concurrent_statements[0]
                    .assignment_kind
                == AssignmentKind::Continuous
            && inventory_unit.concurrent_statements[1].label
                == "direct_call"
            && inventory_unit.concurrent_statements[1].kind
                == StatementKind::ProcedureCall
            && inventory_unit.concurrent_statements[2].label
                == "postponed_assert"
            && inventory_unit.concurrent_statements[2].vhdl_postponed
            && inventory_unit.concurrent_statements[3].label
                == "postponed_call"
            && inventory_unit.concurrent_statements[3].vhdl_postponed,
        "ordinary and postponed concurrent statement HIR");
    require(
        inventory_unit.generate_regions.size() == 1
            && inventory_unit.generate_regions.front()
                    .then_body.concurrent_statements.size()
                == 4
            && inventory_unit.generate_regions.front()
                    .then_body.concurrent_statements[0]
                    .label
                == "generated_select"
            && inventory_unit.generate_regions.front()
                    .then_body.concurrent_statements[1]
                    .label
                == "generated_call"
            && inventory_unit.generate_regions.front()
                .then_body.concurrent_statements[2]
                .vhdl_postponed
            && inventory_unit.generate_regions.front()
                .then_body.concurrent_statements[3]
                .vhdl_postponed,
        "generated ordinary and postponed concurrent statement HIR");
    require(
        inventory_unit.processes.size() == 3
            && inventory_unit.processes[0].name == "reactive"
            && inventory_unit.processes[0].sensitivities.size() == 1
            && inventory_unit.processes[0].sensitivities.front().signal
                == "*"
            && inventory_unit.processes[1].name == "worker"
            && inventory_unit.processes[1].span.begin.offset
                < inventory_unit.processes[1].statements.front().span.begin.offset
            && inventory_unit.processes[2].name == "observer"
            && inventory_unit.processes[2].vhdl_postponed,
        "process(all), postponed process, labels, and complete source span");
    const auto& sequential = inventory_unit.processes[1].statements;
    require(
        sequential.size() == 5
            && sequential[0].label == "sequential_select"
            && sequential[0].kind == StatementKind::Case
            && sequential[0].case_alternatives[0].statements[0].assignment_kind
                == AssignmentKind::VhdlSignal
            && sequential[1].label == "variable_select"
            && sequential[1].case_alternatives[0].statements[0].assignment_kind
                == AssignmentKind::Blocking
            && sequential[2].label == "branch"
            && sequential[2].statements[0].label == "branch_null"
            && sequential[3].label == "choice"
            && sequential[3].case_alternatives[0].statements[0].label
                == "selected_null"
            && sequential[3].case_alternatives[1].statements[0].label
                == "fallback_null"
            && sequential[4].label == "suspended",
        "labeled sequential selected/if/case/simple statement HIR");

    const auto invalid_postponed = parse_text(
        "invalid_postponed.vhd",
        R"(
architecture rtl of invalid_postponed is
  signal result : std_logic;
begin
  postponed result <= '1';
end architecture;
)",
        Language::Vhdl2008);
    require(
        !invalid_postponed.ok()
            && std::ranges::any_of(
                invalid_postponed.diagnostics,
                [](const auto& diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-SEM-104";
                }),
        "postponed must reject ineligible concurrent statements");

    const auto bad_end_labels = parse_text(
        "bad_statement_end_labels.vhd",
        R"(
architecture rtl of bad_statement_end_labels is
begin
  worker: process
  begin
    branch: if true then
      null;
    end if wrong_branch;
    choice: case true is
      when others => null;
    end case wrong_choice;
  end process wrong_worker;
  mixed: process(all, wrong_worker)
  begin
    null;
  end process mixed;
end architecture;
)",
        Language::Vhdl2008);
    require(
        !bad_end_labels.ok()
            && std::ranges::count_if(
                   bad_end_labels.diagnostics,
                   [](const auto& diagnostic) {
                       return diagnostic.code
                           == "FSIM-VHDL-SEM-084";
                   })
                == 3,
        "mismatched if/case/process end labels are targeted");
    require(
        std::ranges::any_of(
            bad_end_labels.diagnostics,
            [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-085";
            }),
        "process(all) cannot mix explicit sensitivity names");
}

void test_vhdl_delay_mechanisms()
{
    const auto parsed = parse_text(
        "delay_mechanisms.vhd",
        R"(
entity delay_mechanisms is
end entity;

architecture rtl of delay_mechanisms is
  signal selector : std_logic;
  signal source : std_logic;
  signal implicit_value : std_logic;
  signal inertial_value : std_logic;
  signal transport_value : std_logic;
  signal rejected_value : std_logic;
  signal selected_value : std_logic;
begin
  implicit_value <= source after 5 ns;
  inertial_value <= inertial source after 5 ns;
  transport_value <= transport source after 5 ns;
  rejected_value <= reject 2 ns inertial source after 5 ns;
  with selector select
    selected_value <= reject 1 ns inertial
      source after 3 ns when '1',
      '0' after 3 ns when others;

  sequential: process(source)
    variable local_value : std_logic;
  begin
    local_value := source;
    transport_value <= transport source after 4 ns;
  end process;
end architecture;
)",
        Language::Vhdl2008);
    require(parsed.ok(), "VHDL delay mechanisms must parse");
    const auto* architecture = parsed.design.find(UnitKind::VhdlArchitecture, "rtl");
    require(
        architecture
            && architecture->concurrent_statements.size() == 5
            && architecture->processes.size() == 1,
        "VHDL delay mechanism statement contexts");
    const auto& implicit_value = architecture->concurrent_statements[0];
    const auto& inertial_value = architecture->concurrent_statements[1];
    const auto& transport_value = architecture->concurrent_statements[2];
    const auto& rejected_value = architecture->concurrent_statements[3];
    require(
        implicit_value.vhdl_delay_mechanism
                == VhdlDelayMechanism::ImplicitInertial
            && inertial_value.vhdl_delay_mechanism
                == VhdlDelayMechanism::Inertial
            && transport_value.vhdl_delay_mechanism
                == VhdlDelayMechanism::Transport
            && rejected_value.vhdl_rejection_limit
            && rejected_value.vhdl_rejection_limit->magnitude == 2
            && rejected_value.vhdl_rejection_limit->unit == "ns",
        "simple VHDL delay mechanism HIR");
    const auto& alternatives = architecture->concurrent_statements[4].case_alternatives;
    require(
        alternatives.size() == 2
            && alternatives[0].statements[0].vhdl_delay_mechanism
                == VhdlDelayMechanism::Inertial
            && alternatives[1].statements[0].vhdl_rejection_limit
            && alternatives[1]
                    .statements[0]
                    .vhdl_rejection_limit->magnitude
                == 1,
        "a selected assignment propagates its common delay mechanism");
    const auto& sequential = architecture->processes.front().statements;
    require(
        sequential.size() == 2
            && !sequential[0].vhdl_delay_mechanism
            && sequential[1].vhdl_delay_mechanism
                == VhdlDelayMechanism::Transport,
        "VHDL variable assignments stay distinct from signal mechanisms");

    const auto guarded = parse_text(
        "guarded_assignments.vhd",
        R"(architecture rtl of guarded_assignments is
  signal enabled : boolean; signal selector, source, result : std_logic;
begin
  scope: block (enabled) is
    signal local_explicit, local_other : std_logic;
    disconnect local_explicit : std_logic after 2 ns;
    disconnect others : std_logic after 3 ns;
  begin
    simple: result <= guarded transport null after 1 ns,
                                      source after 3 ns;
    result <= guarded source when enabled else null;
    with selector select result <= guarded reject 1 ns inertial
      source after 2 ns when '1', null after 2 ns when others;
    local_explicit <= guarded source;
    local_other <= guarded source;
  end block scope;
  all_scope: block (enabled) is
    signal local_all : std_logic;
    disconnect all : std_logic after 4 ns;
  begin
    local_all <= guarded source;
  end block all_scope;
  process begin result <= guarded source; wait; end process;
end architecture;)",
        Language::Vhdl2008);
    const auto& guarded_body = guarded.design.units.front().generate_regions.front().then_body;
    const auto& all_body = guarded.design.units.front().generate_regions[1].then_body;
    require(
        !guarded.ok()
            && guarded_body.concurrent_statements.size() == 5
            && guarded_body.concurrent_statements[0]
                .vhdl_guarded_assignment
            && guarded_body.concurrent_statements[0]
                .vhdl_waveform.front()
                .disconnect
            && guarded_body.concurrent_statements[0]
                    .vhdl_waveform.back()
                    .value.text
                == "source"
            && guarded_body.concurrent_statements[1]
                .vhdl_guarded_assignment
            && guarded_body.concurrent_statements[1]
                .else_statements.front()
                .vhdl_waveform.front()
                .disconnect
            && guarded_body.concurrent_statements[2]
                .vhdl_guarded_assignment
            && guarded_body.concurrent_statements[2]
                .case_alternatives.back()
                .statements.front()
                .vhdl_waveform.front()
                .disconnect
            && guarded_body.concurrent_statements[3]
                .vhdl_disconnection_delay
            && guarded_body.concurrent_statements[3]
                    .vhdl_disconnection_delay->magnitude
                == 2
            && guarded_body.concurrent_statements[4]
                .vhdl_disconnection_delay
            && guarded_body.concurrent_statements[4]
                    .vhdl_disconnection_delay->magnitude
                == 3
            && all_body.concurrent_statements.size() == 1
            && all_body.concurrent_statements.front()
                .vhdl_disconnection_delay
            && all_body.concurrent_statements.front()
                    .vhdl_disconnection_delay->magnitude
                == 4
            && std::ranges::any_of(
                guarded.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-SEM-087";
                }),
        "guarded simple, conditional, selected, null, and context HIR");

    const auto invalid_disconnections = parse_text(
        "invalid_disconnections.vhd",
        R"(architecture rtl of invalid_disconnections is
  signal enabled : boolean;
begin
  scope: block (enabled) is
    signal value : std_logic;
    disconnect value : integer after 1 ns;
    disconnect all : std_logic after 2 ns;
    disconnect all : std_logic after 3 ns;
  begin
    value <= guarded '1';
  end block scope;
end architecture;)",
        Language::Vhdl2008);
    require(
        !invalid_disconnections.ok()
            && std::ranges::any_of(
                invalid_disconnections.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-SEM-105";
                })
            && std::ranges::any_of(
                invalid_disconnections.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-SEM-106";
                }),
        "disconnection type mismatch and overlapping selections are targeted");

    const auto malformed = parse_text(
        "bad_delay_mechanisms.vhd",
        R"(
architecture rtl of bad_delay_mechanisms is
  signal source : std_logic;
  signal result : std_logic;
begin
  result <= reject -1 ns transport source after 5 ns;
  result <= source transport after 5 ns;
end architecture;
)",
        Language::Vhdl2008);
    const auto has_code = [&](const std::string_view code) {
        return std::ranges::any_of(
            malformed.diagnostics,
            [code](const auto& diagnostic) {
                return diagnostic.code == code;
            });
    };
    require(
        !malformed.ok()
            && has_code("FSIM-VHDL-SEM-031")
            && has_code("FSIM-VHDL-PARSE-123")
            && has_code("FSIM-VHDL-PARSE-124"),
        "malformed and misplaced VHDL delay mechanisms are targeted");
}

void test_vhdl_ordered_waveforms()
{
    const auto parsed = parse_text(
        "ordered_waveforms.vhd",
        R"(
architecture rtl of ordered_waveforms is
  signal choose : boolean;
  signal selector : std_logic;
  signal source : std_logic_vector(1 downto 0);
  signal result : std_logic_vector(1 downto 0);
begin
  result <= transport "00" after 1 ns, "11" after 4 ns;
  result <= "01" after 2 ns, "10" after 6 ns
            when choose else unaffected;
  with selector select
    result <= "00" after 1 ns, "11" after 3 ns when '0',
              unaffected when others;
  process
  begin
    result(1 downto 0) <=
        inertial "10" after 2 ns, "01" after 5 ns;
    result <= force out "11";
    result <= release out;
    wait;
  end process;
end architecture;
)",
        Language::Vhdl2008);
    require(parsed.ok(), "ordered VHDL waveforms must parse");
    const auto& unit = parsed.design.units.front();
    require(
        unit.concurrent_statements.size() == 3
            && unit.processes.size() == 1,
        "ordered waveform statement contexts");
    const auto& simple = unit.concurrent_statements[0];
    require(
        simple.kind == StatementKind::Assignment
            && simple.vhdl_waveform.size() == 2
            && simple.vhdl_waveform[0].delay
            && simple.vhdl_waveform[0].delay->magnitude == 1
            && simple.vhdl_waveform[1].delay
            && simple.vhdl_waveform[1].delay->magnitude == 4,
        "simple ordered waveform HIR");
    const auto& conditional = unit.concurrent_statements[1];
    require(
        conditional.kind == StatementKind::If
            && conditional.statements[0].vhdl_waveform.size() == 2
            && conditional.else_statements[0].vhdl_unaffected,
        "conditional alternatives retain independent waveform state");
    const auto& selected = unit.concurrent_statements[2];
    require(
        selected.kind == StatementKind::Case
            && selected.case_alternatives.size() == 2
            && selected.case_alternatives[0]
                    .statements[0]
                    .vhdl_waveform.size()
                == 2
            && selected.case_alternatives[1]
                .statements[0]
                .vhdl_unaffected,
        "selected alternatives retain waveform or unaffected");
    require(
        unit.processes[0].statements[0].vhdl_waveform.size() == 2
            && unit.processes[0]
                    .statements[0]
                    .target.kind
                == ExpressionKind::Slice,
        "sequential slice waveform HIR");
    require(
        unit.processes[0].statements.size() == 4
            && unit.processes[0].statements[1].kind
                == StatementKind::Force
            && unit.processes[0].statements[1].value.text == "\"11\""
            && unit.processes[0].statements[1].vhdl_force_driving_value
            && unit.processes[0].statements[2].kind
                == StatementKind::Release
            && unit.processes[0].statements[2].vhdl_force_driving_value,
        "sequential VHDL force and release retain executable statement HIR");

    const auto malformed = parse_text(
        "malformed_waveforms.vhd",
        R"(
architecture rtl of malformed_waveforms is
  signal result : std_logic;
begin
  result <= unaffected, '1' after 1 ns;
  result <= ;
  result <= null after 1 ns;
end architecture;
)",
        Language::Vhdl2008);
    const auto has_code = [&](const std::string_view code) {
        return std::ranges::any_of(
            malformed.diagnostics,
            [code](const auto& diagnostic) {
                return diagnostic.code == code;
            });
    };
    require(
        !malformed.ok()
            && has_code("FSIM-VHDL-PARSE-125")
            && has_code("FSIM-VHDL-PARSE-126")
            && has_code("FSIM-VHDL-UNSUPPORTED-025"),
        "malformed, empty, and null VHDL waveforms are targeted (mixed="
            + std::to_string(has_code("FSIM-VHDL-PARSE-125"))
            + ", empty="
            + std::to_string(has_code("FSIM-VHDL-PARSE-126"))
            + ", null="
            + std::to_string(has_code("FSIM-VHDL-UNSUPPORTED-025"))
            + ")");
}

void test_vhdl_case_statements()
{
    const auto result = parse_text(
        "case_statement.vhd",
        R"(
entity case_statement is
end entity;
architecture rtl of case_statement is
  signal selector : std_logic_vector(1 downto 0);
  signal result : std_logic;
begin
  choose: process(selector)
  begin
    case selector is
      when "00" | "01" =>
        result <= '0';
      when others =>
        result <= '1';
    end case;
  end process;
end architecture;
)",
        Language::Vhdl2008);
    require(result.ok(), "VHDL sequential case statement must parse");
    const auto* architecture = result.design.find(UnitKind::VhdlArchitecture, "rtl");
    require(
        architecture != nullptr
            && architecture->processes.size() == 1
            && architecture->processes.front().statements.size() == 1
            && architecture->processes.front().statements[0].kind
                == StatementKind::Case
            && architecture->processes.front()
                    .statements[0]
                    .case_alternatives.size()
                == 2
            && architecture->processes.front()
                    .statements[0]
                    .case_alternatives[0]
                    .choices.size()
                == 2
            && architecture->processes.front()
                .statements[0]
                .case_alternatives[1]
                .is_default,
        "VHDL case alternatives and choices are retained");

    const auto ranges = parse_text(
        "case_ranges.vhd",
        R"(architecture rtl of case_ranges is begin
  process begin
    case 4 is
      when 0 | 1 to 3 => null;
      when 6 downto 4 | 8 to 7 => null;
      when others => null;
    end case;
  end process;
end architecture;)",
        Language::Vhdl2008);
    require(ranges.ok(), "VHDL discrete case ranges must parse");
    const auto& range_case = ranges.design.units.front().processes.front().statements.front();
    require(
        range_case.case_alternatives.size() == 3
            && range_case.case_alternatives[0].choices.size() == 2
            && range_case.case_alternatives[0].choices[1].kind
                == ExpressionKind::Call
            && range_case.case_alternatives[0].choices[1].text
                == "@vhdl-case-range-to"
            && range_case.case_alternatives[0].choices[1].operands.size() == 2
            && range_case.case_alternatives[1].choices.size() == 2
            && range_case.case_alternatives[1].choices[0].text
                == "@vhdl-case-range-downto"
            && range_case.case_alternatives[2].is_default,
        "grouped ascending, descending, null, and others choices are retained");

    const auto duplicate_others = parse_text(
        "duplicate_others.vhd",
        R"(
architecture rtl of duplicate_others is
begin
  choose: process
  begin
    case "00" is
      when others => null;
      when others => null;
    end case;
  end process;
end architecture;
)",
        Language::Vhdl2008);
    require(
        !duplicate_others.ok()
            && std::ranges::any_of(
                duplicate_others.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-SEM-021";
                })
            && std::ranges::any_of(
                duplicate_others.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-SEM-022";
                }),
        "duplicate and nonfinal VHDL case others alternatives are targeted");

    const auto matching = parse_text(
        "matching_statements.vhd",
        R"(
architecture rtl of matching_statements is
  signal selector : std_logic_vector(3 downto 0);
  signal result : integer;
begin
  choose: process(selector)
    variable selected : integer;
  begin
    matching_case: case? selector is
      when "1001" => selected := 1;
      when others => selected := 0;
    end case? matching_case;
    matching_select: with selector select?
      selected := 2 when "10--", selected when others;
    result <= 3 when selector ?= "1---" else selected;
  end process choose;
end architecture;
)",
        Language::Vhdl2008);
    require(matching.ok(), "VHDL-2008 matching statements must parse");
    const auto& matching_statements = matching.design.units.front().processes.front().statements;
    require(
        matching_statements.size() == 3
            && matching_statements[0].case_match_kind
                == CaseMatchKind::VhdlMatching
            && matching_statements[1].case_match_kind
                == CaseMatchKind::VhdlMatching
            && matching_statements[2].kind == StatementKind::If
            && matching_statements[2].condition.kind
                == ExpressionKind::Binary
            && matching_statements[2].condition.text == "?=",
        "matching case, selected assignment, and conditional operator HIR");

    const auto mismatched_marker = parse_text(
        "mismatched_matching_case.vhd",
        R"(architecture rtl of mismatched_matching_case is begin
  process begin case? '0' is when others => null; end case; end process;
end architecture;)",
        Language::Vhdl2008);
    require(
        !mismatched_marker.ok()
            && std::ranges::any_of(
                mismatched_marker.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-SEM-086";
                }),
        "matching case opening and ending markers must agree");
}
void test_vhdl_revision_expression_profiles()
{
    const auto diagnostic_count = [](const auto& diagnostics,
                                      const std::string_view code) {
        return std::ranges::count_if(
            diagnostics, [&](const Diagnostic& diagnostic) {
                return diagnostic.code == code;
            });
    };
    const auto parse_architecture = [](const std::string_view name,
                                        const std::string_view declarations,
                                        const std::string_view statements,
                                        const VhdlStandard standard) {
        return parse_text(
            std::string { name } + ".vhd",
            "architecture rtl of " + std::string { name } + " is\n"
                + std::string { declarations } + "\nbegin\n"
                + std::string { statements } + "\nend architecture;\n",
            Language::Vhdl2008,
            standard);
    };

    constexpr std::array older_standards {
        VhdlStandard::Vhdl1987,
        VhdlStandard::Vhdl1993,
        VhdlStandard::Vhdl2000,
        VhdlStandard::Vhdl2002
    };
    for (const auto standard : older_standards) {
        const auto parsed = parse_architecture(
            "older_expression",
            R"(  type wide_t is array (integer range <>) of bit;
  subtype exact_wide_t is wide_t(105 downto -31);
  subtype null_t is wide_t(0 downto 1);
  function select_wide(value : exact_wide_t; amount : integer := 0)
      return exact_wide_t is
  begin
    return value;
  end function;
  signal source : exact_wide_t;
  signal result : exact_wide_t;
  signal empty : null_t;)",
            R"(  result <= select_wide(
    value => exact_wide_t'(source xor source), amount => 0);
  empty <= (others => '0');)",
            standard);
        require(
            parsed.ok(),
            "qualification, named association, universal integer values, "
            "null ranges, and 137-bit expressions remain legal in every "
            "older VHDL revision");
        const auto& unit = parsed.design.units.front();
        require(
            unit.concurrent_statements.size() == 2U
                && unit.concurrent_statements.front().value.kind
                    == ExpressionKind::Call
                && unit.concurrent_statements.front().value.call_argument_names
                    == std::vector<std::string>({ "value", "amount" }),
            "older expressions retain qualification and named actual order");
        const auto null_type = std::ranges::find_if(
            unit.type_aliases, [](const TypeAliasDeclaration& declaration) {
                return declaration.name == "null_t";
            });
        require(
            null_type != unit.type_aliases.end()
                && null_type->type.vhdl_array_constraints.size() == 1U
                && null_type->type.vhdl_array_constraints.front()
                        .left.text
                    == "0"
                && null_type->type.vhdl_array_constraints.front()
                        .right.text
                    == "1"
                && null_type->type.vhdl_array_constraints.front().descending,
            "older expressions retain an exact null array range without "
            "host-word narrowing");
    }

    const auto vhdl_1993 = parse_architecture(
        "vhdl93_operators",
        "  signal source : bit_vector(136 downto 0);\n"
        "  signal result : bit_vector(136 downto 0);",
        "  result <= (source xnor source) sll 1;",
        VhdlStandard::Vhdl1993);
    require(
        vhdl_1993.ok()
            && vhdl_1993.design.units.front().concurrent_statements.front().value.text
                == "sll"
            && vhdl_1993.design.units.front().concurrent_statements.front().value.operands.front().text
                == "xnor",
        "VHDL-1993 adds xnor and shift/rotate operators without narrowing a "
        "137-bit expression");

    const auto vhdl_2008 = parse_architecture(
        "vhdl2008_expressions",
        "  signal source : bit_vector(136 downto 0);\n"
        "  signal flag : boolean;\n"
        "  signal scalar : bit;\n"
        "  signal number : integer;",
        "  flag <= and source;\n"
        "  flag <= ?? scalar;\n"
        "  number <= 1 when flag else 2;\n"
        "  number <= case flag is when true => 3, when false => 4;",
        VhdlStandard::Vhdl2008);
    const auto& vhdl_2008_statements = vhdl_2008.design.units.front().concurrent_statements;
    require(
        vhdl_2008.ok() && vhdl_2008_statements.size() == 4U,
        "VHDL-2008 expression forms parse without diagnostics");
    require(
        vhdl_2008_statements[0].value.kind == ExpressionKind::Unary
            && vhdl_2008_statements[0].value.text == "and",
        "VHDL-2008 unary reduction retains its source operation");
    require(
        vhdl_2008_statements[1].value.kind == ExpressionKind::Unary
            && vhdl_2008_statements[1].value.text == "??",
        "VHDL-2008 unary condition retains its source operation");
    require(
        vhdl_2008_statements[3].value.text == "?:",
        "VHDL-2008 conditional and case expressions retain typed source "
        "structure");

    const auto expect_revision_error = [&](const std::string_view name,
                                           const std::string_view statement,
                                           const VhdlStandard standard,
                                           const std::string_view feature) {
        const auto parsed = parse_architecture(
            name,
            "  signal source : bit_vector(136 downto 0);\n"
            "  signal result : bit_vector(136 downto 0);\n"
            "  signal flag : boolean;\n"
            "  signal number : integer;",
            statement,
            standard);
        require(
            diagnostic_count(parsed.diagnostics, "FSIM-FE-VHSTD-003") == 1U
                && std::ranges::any_of(
                    parsed.diagnostics,
                    [&](const Diagnostic& diagnostic) {
                        return diagnostic.code == "FSIM-FE-VHSTD-003"
                            && diagnostic.message.find(feature)
                            != std::string::npos;
                    }),
            std::string { name }
                + " must have one exact revision diagnostic and an "
                  "actionable feature name");
    };
    expect_revision_error(
        "vhdl87_xnor", "  result <= source xnor source;",
        VhdlStandard::Vhdl1987, "xnor");
    expect_revision_error(
        "vhdl87_shift", "  result <= source sll 1;",
        VhdlStandard::Vhdl1987, "sll");
    expect_revision_error(
        "vhdl2002_reduction", "  flag <= and source;",
        VhdlStandard::Vhdl2002, "reduction");
    expect_revision_error(
        "vhdl2002_condition", "  flag <= ?? flag;",
        VhdlStandard::Vhdl2002, "condition operator");
    expect_revision_error(
        "vhdl2002_conditional",
        "  number <= integer'(1 when flag else 2);",
        VhdlStandard::Vhdl2002, "conditional expression");
    expect_revision_error(
        "vhdl2002_case_expression",
        "  number <= case flag is when true => 1, when false => 0;",
        VhdlStandard::Vhdl2002, "case expression");

    const auto malformed = parse_architecture(
        "vhdl93_malformed_operator",
        "  signal source : bit_vector(136 downto 0);\n"
        "  signal result : bit_vector(136 downto 0);",
        "  result <= source xnor;",
        VhdlStandard::Vhdl1993);
    require(
        diagnostic_count(malformed.diagnostics, "FSIM-FE-VHSTD-003") == 0U
            && diagnostic_count(
                   malformed.diagnostics, "FSIM-VHDL-PARSE-035")
                == 1U,
        "malformed available syntax remains distinct from revision "
        "availability");
}

} // namespace fsim::tests::frontend

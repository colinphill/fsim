// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[maybe_unused]] std::filesystem::path make_test_directory(
    std::string_view name) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-" + std::string{name} + "-"
         + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  return directory;
}

[[maybe_unused]] void write_text(
    const std::filesystem::path& path,
    const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  require(
      output.good(),
      "frontend test fixture must be writable");
}

} // namespace

void test_vhdl_selected_assignments() {
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
  const auto* architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture
          && architecture->concurrent_statements.size() == 1,
      "a selected signal assignment must be retained as one statement");
  const auto& selected =
      architecture->concurrent_statements.front();
  require(
      selected.kind == StatementKind::Case
          && selected.label == "choose"
          && selected.condition.text == "selector"
          && selected.case_alternatives.size() == 2
          && selected.case_alternatives[0].choices.size() == 2
          && selected.case_alternatives[0].statements.size() == 1
          && selected.case_alternatives[0]
                 .statements.front().delay
                 .has_value()
          && selected.case_alternatives[0]
                 .statements.front().delay->magnitude
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
                 .then_body.concurrent_statements[0].label
              == "generated_select"
          && inventory_unit.generate_regions.front()
                 .then_body.concurrent_statements[1].label
              == "generated_call"
          && inventory_unit.generate_regions.front()
                 .then_body.concurrent_statements[2].vhdl_postponed
          && inventory_unit.generate_regions.front()
                 .then_body.concurrent_statements[3].vhdl_postponed,
      "generated ordinary and postponed concurrent statement HIR");
  require(
      inventory_unit.processes.size() == 3
          && inventory_unit.processes[0].name == "reactive"
          && inventory_unit.processes[0].sensitivities.size() == 1
          && inventory_unit.processes[0].sensitivities.front().signal
              == "*"
          && inventory_unit.processes[1].name == "worker"
          && inventory_unit.processes[1].span.begin.offset
              < inventory_unit.processes[1].statements.front()
                    .span.begin.offset
          && inventory_unit.processes[2].name == "observer"
          && inventory_unit.processes[2].vhdl_postponed,
      "process(all), postponed process, labels, and complete source span");
  const auto& sequential =
      inventory_unit.processes[1].statements;
  require(
      sequential.size() == 5
          && sequential[0].label == "sequential_select"
          && sequential[0].kind == StatementKind::Case
          && sequential[0].case_alternatives[0]
                 .statements[0].assignment_kind
              == AssignmentKind::VhdlSignal
          && sequential[1].label == "variable_select"
          && sequential[1].case_alternatives[0]
                 .statements[0].assignment_kind
              == AssignmentKind::Blocking
          && sequential[2].label == "branch"
          && sequential[2].statements[0].label == "branch_null"
          && sequential[3].label == "choice"
          && sequential[3].case_alternatives[0]
                 .statements[0].label
              == "selected_null"
          && sequential[3].case_alternatives[1]
                 .statements[0].label
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

void test_vhdl_delay_mechanisms() {
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
  const auto* architecture =
      parsed.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture
          && architecture->concurrent_statements.size() == 5
          && architecture->processes.size() == 1,
      "VHDL delay mechanism statement contexts");
  const auto& implicit_value =
      architecture->concurrent_statements[0];
  const auto& inertial_value =
      architecture->concurrent_statements[1];
  const auto& transport_value =
      architecture->concurrent_statements[2];
  const auto& rejected_value =
      architecture->concurrent_statements[3];
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
  const auto& alternatives =
      architecture->concurrent_statements[4].case_alternatives;
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
  const auto& sequential =
      architecture->processes.front().statements;
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
  const auto& guarded_body =
      guarded.design.units.front().generate_regions.front().then_body;
  const auto& all_body =
      guarded.design.units.front().generate_regions[1].then_body;
  require(
      !guarded.ok()
          && guarded_body.concurrent_statements.size() == 5
          && guarded_body.concurrent_statements[0]
                 .vhdl_guarded_assignment
          && guarded_body.concurrent_statements[0]
                 .vhdl_waveform.front().disconnect
          && guarded_body.concurrent_statements[0]
                 .vhdl_waveform.back().value.text == "source"
          && guarded_body.concurrent_statements[1]
                 .vhdl_guarded_assignment
          && guarded_body.concurrent_statements[1]
                 .else_statements.front().vhdl_waveform.front().disconnect
          && guarded_body.concurrent_statements[2]
                 .vhdl_guarded_assignment
          && guarded_body.concurrent_statements[2]
                 .case_alternatives.back().statements.front()
                 .vhdl_waveform.front().disconnect
          && guarded_body.concurrent_statements[3]
                 .vhdl_disconnection_delay
          && guarded_body.concurrent_statements[3]
                 .vhdl_disconnection_delay->magnitude == 2
          && guarded_body.concurrent_statements[4]
                 .vhdl_disconnection_delay
          && guarded_body.concurrent_statements[4]
                 .vhdl_disconnection_delay->magnitude == 3
          && all_body.concurrent_statements.size() == 1
          && all_body.concurrent_statements.front()
                 .vhdl_disconnection_delay
          && all_body.concurrent_statements.front()
                 .vhdl_disconnection_delay->magnitude == 4
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

void test_vhdl_ordered_waveforms() {
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

void test_vhdl_case_statements() {
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
  const auto* architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
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
  const auto& range_case =
      ranges.design.units.front().processes.front().statements.front();
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
  const auto& matching_statements =
      matching.design.units.front().processes.front().statements;
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

void test_vhdl_sequential_for_loops() {
  const auto result = parse_text(
      "sequential_for.vhd",
      R"(
entity sequential_for is
end entity;
architecture rtl of sequential_for is
  signal ascending : std_logic_vector(2 downto 0);
  signal descending : std_logic_vector(3 downto 1);
begin
  populate: process
  begin
    for lane in 0 to 2 loop
      ascending(lane) <= '1';
    end loop;
    for lane in 3 downto 1 loop
      descending(lane) <= '0';
    end loop;
    while false loop
      null;
    end loop;
    wait for 1 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(result.ok(), "VHDL sequential for loops must parse");
  const auto* architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture != nullptr
          && architecture->processes.size() == 1
          && architecture->processes.front().statements.size() == 4,
      "VHDL sequential for-loop process");
  const auto& ascending =
      architecture->processes.front().statements[0];
  const auto& descending =
      architecture->processes.front().statements[1];
  require(
      ascending.kind == StatementKind::Loop
          && ascending.loop_variable == "lane"
          && !ascending.loop_descending
          && ascending.loop_initial.text == "0"
          && ascending.loop_limit.text == "2"
          && ascending.statements.size() == 1
          && descending.kind == StatementKind::Loop
          && descending.loop_descending
          && descending.loop_initial.text == "3"
          && descending.loop_limit.text == "1"
          && architecture->processes.front()
                 .statements[2]
                 .kind == StatementKind::Loop
          && architecture->processes.front()
                 .statements[2]
                 .loop_runtime
          && architecture->processes.front()
                 .statements[2]
                 .condition.kind
              == ExpressionKind::BooleanLiteral,
      "VHDL sequential for-loop ranges and bodies are retained");

  const auto labeled_end = parse_text(
      "labeled_sequential_for.vhd",
      R"(
architecture rtl of labeled_sequential_for is
begin
  populate: process
  begin
    populate_loop: for lane in 0 to 1 loop
      null;
    end loop populate_loop;
    wait for 1 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      labeled_end.ok()
          && labeled_end.design.units.back()
                 .processes.front()
                 .statements.front()
                 .loop_label
              == "populate_loop",
      "matching opening and end labels are retained on VHDL loops");
}

void test_systemverilog_vertical_slice() {
  constexpr std::string_view source = R"(
module counter(
  input logic clk,
  output logic [7:0] q
);
  logic [7:0] next;
  assign next = q + 1;
  child u_child(.value(q), .result(next));

  always_ff @(posedge clk) begin
    q <= next;
  end

  initial begin
    #10 $finish;
  end
endmodule
)";

  const auto result =
      parse_text("counter.sv", source, Language::SystemVerilog2017);
  require(result.ok(),
          "SystemVerilog vertical slice must parse without errors");
  require(result.design.units.size() == 1, "SV module count");
  const auto& module = result.design.units.front();
  require(module.kind == UnitKind::VerilogModule, "SV unit kind");
  require(module.name == "counter", "SV module name");
  require(module.ports.size() == 2, "SV ANSI port count");
  require(module.ports[1].type.width() == 8, "SV vector width");
  require(module.signals.size() == 1, "SV signal declaration");
  require(module.concurrent_statements.size() == 1,
          "SV continuous assignment");
  require(module.instances.size() == 1, "SV module instance");
  require(module.instances.front().unit_name == "child"
              && module.instances.front().name == "u_child",
          "SV instance unit and name");
  require(module.instances.front().connections.size() == 2
              && module.instances.front().connections.front().port
                  == std::optional<std::string>{"value"},
          "SV named port connection");
  require(module.processes.size() == 2, "SV process count");

  const auto& always = module.processes[0];
  require(always.kind == ProcessKind::SystemVerilogAlwaysFF,
          "always_ff process kind");
  require(always.sensitivities.size() == 1 &&
              always.sensitivities.front().edge == EdgeKind::Positive,
          "SV posedge sensitivity");
  require(always.statements.size() == 1 &&
              always.statements.front().assignment_kind ==
                  AssignmentKind::NonBlocking,
          "SV nonblocking assignment");

  const auto& initial = module.processes[1];
  require(initial.kind == ProcessKind::Initial, "initial process kind");
  require(initial.statements.size() == 1 &&
              initial.statements.front().kind == StatementKind::Delay,
          "initial delay statement");
  require(initial.statements.front().delay->magnitude == 10,
          "Verilog delay magnitude");
  require(initial.statements.front().statements.size() == 1 &&
              initial.statements.front().statements.front().kind ==
                  StatementKind::Finish,
          "delayed $finish");
}

void test_non_ansi_verilog_ports() {
  constexpr std::string_view source = R"(
module edge_reg(clk, d, q);
  input clk, d;
  output q;
  reg q;
  always @(posedge clk) q <= d;
endmodule
)";
  const auto result =
      parse_text("edge_reg.v", source, Language::Verilog2005);
  require(result.ok(), "Verilog-2005 non-ANSI module must parse");
  const auto& module = result.design.units.front();
  require(module.ports.size() == 3,
          "body port declarations must update header placeholders");
  require(module.signals.empty(),
          "a non-ANSI output reg must not become a duplicate signal");
  require(module.ports[2].type.spelling == "reg",
          "a non-ANSI reg redeclaration must refine the port type");
  require(module.processes.front().kind == ProcessKind::VerilogAlways,
          "Verilog always process kind");

  const auto duplicate = parse_text(
      "duplicate_non_ansi_port.v",
      R"(
module duplicate_non_ansi_port(value);
  input value;
  input value;
endmodule
)",
      Language::Verilog2005);
  require(!duplicate.ok(), "duplicate non-ANSI ports must fail");
  require(
      std::ranges::any_of(
          duplicate.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-004";
          }),
      "duplicate non-ANSI ports have a stable diagnostic");
}

void test_diagnostics_and_spans() {
  const auto result = parse_text(
      "broken.sv", "module broken(input logic a)\nassign = a;\nendmodule",
      Language::SystemVerilog2017);
  require(!result.ok(), "malformed source must fail");
  require(!result.diagnostics.empty(), "malformed source diagnostic");
  require(result.diagnostics.front().span.source_name == "broken.sv",
          "diagnostic source name");
  require(result.diagnostics.front().span.begin.line >= 1,
          "diagnostic line is one-based");
  require(result.diagnostics.front().code.starts_with("FSIM-"),
          "diagnostics have stable codes");
}

void test_vhdl_context_diagnostics() {
  const auto declaration = parse_text(
      "context_declaration.vhd",
      R"(
context shared is
  library ieee;
  use ieee.std_logic_1164.all;
  context work.base;
end context shared;
context work.shared;
entity context_user is
end entity;
)",
      Language::Vhdl2008);
  require(
      declaration.ok() && declaration.design.units.size() == 2,
      "bounded context declaration and following unit must parse");
  require(
      declaration.design.units[0].kind == UnitKind::VhdlContext
          && declaration.design.units[0].name == "shared"
          && declaration.design.units[0].vhdl_context.size() == 3
          && declaration.design.units[0]
                 .vhdl_context.back()
                 .kind
              == VhdlContextItemKind::ContextReference,
      "context declaration retains reusable context items");
  require(
      declaration.design.units[1].vhdl_context.size() == 1
          && declaration.design.units[1]
                 .vhdl_context.front()
                 .selected_names.front()
              == "work.shared",
      "context reference remains attached to the following unit");

  const auto malformed = parse_text(
      "malformed_context.vhd",
      "use ieee.; entity context_user is end entity;",
      Language::Vhdl2008);
  require(!malformed.ok(), "a malformed context clause must be rejected");
  require(
      std::any_of(
          malformed.diagnostics.begin(),
          malformed.diagnostics.end(),
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-VHDL-PARSE-044";
          }),
      "malformed context clause needs a targeted diagnostic");

  const auto adjacent_identifiers = parse_text(
      "adjacent_context_identifiers.vhd",
      "library e is; entity recovered is end entity;",
      Language::Vhdl2008);
  require(
      !adjacent_identifiers.ok()
          && std::any_of(
              adjacent_identifiers.diagnostics.begin(),
              adjacent_identifiers.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-PARSE-044";
              }),
      "adjacent context identifiers must be consumed and diagnosed");

  const auto invalid_declaration = parse_text(
      "invalid_context_declaration.vhd",
      R"(
context invalid is
  signal not_a_context_item : bit;
end context mismatched;
entity recovered is
end entity recovered;
)",
      Language::Vhdl2008);
  require(
      !invalid_declaration.ok()
          && invalid_declaration.design.units.size() == 2
          && std::any_of(
              invalid_declaration.diagnostics.begin(),
              invalid_declaration.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-UNSUPPORTED-024";
              })
          && std::any_of(
              invalid_declaration.diagnostics.begin(),
              invalid_declaration.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-092";
              }),
      "invalid context declaration items and end names are targeted");
}

void test_vhdl_package_constants() {
  const auto parsed = parse_text(
      "package_constants.vhd",
      R"(
library support;
package constants is
  constant width, lanes : natural := 4;
  constant next_value : integer := width + 1;
  constant enabled : boolean := true;
  constant initial_bit : bit := '1';
  constant deferred_vector : bit_vector(256 downto 0);
end package constants;

package body constants is
  constant deferred_vector : bit_vector(256 downto 0) := (others => '0');
end package body constants;

use work.constants.all;
entity package_user is
end entity package_user;
)",
      Language::Vhdl2008);
  require(parsed.ok(), "bounded VHDL package constants must parse");
  require(
      parsed.design.units.size() == 3,
      "package, package body, and following entity are retained");
  const auto& package = parsed.design.units[0];
  require(
      package.kind == UnitKind::VhdlPackage
          && package.name == "constants"
          && package.parameters.size() == 6,
      "package declaration records each constant");
  require(
      package.parameters[0].name == "width"
          && package.parameters[1].name == "lanes"
          && package.parameters[2].default_value.kind
              == ExpressionKind::Binary
          && package.parameters[3].type.domain
              == ValueDomain::Boolean
          && package.parameters[4].type.domain
              == ValueDomain::Bit2
          && package.parameters[5].vhdl_deferred
          && package.parameters[5].type.width() == 257,
      "package constants retain scalar, arbitrary-width, and deferred forms");
  require(
      package.vhdl_context.size() == 1
          && package.vhdl_context.front().kind
              == VhdlContextItemKind::LibraryClause,
      "package retains its own context");
  require(
      parsed.design.units[2].vhdl_context.size() == 1
          && parsed.design.units[2]
                 .vhdl_context.front()
                 .selected_names.front()
              == "work.constants.all",
      "following unit retains package use visibility");

  const auto selected_names = parse_text(
      "selected_package_names.vhd",
      R"(
entity selected_package_names is
  port (
    observed : out unsigned(
      work.constants.width - 1 downto 0)
  );
end entity selected_package_names;
architecture rtl of selected_package_names is
begin
  observed <= constants.next_value;
end architecture rtl;
)",
      Language::Vhdl2008);
  require(
      selected_names.ok()
          && selected_names.design.units.size() == 2
          && selected_names.design.units[0]
                 .ports.front()
                 .type.packed_range_expression
          && selected_names.design.units[0]
                 .ports.front()
                 .type.packed_range_expression
                 ->left.operands.front().text
              == "work.constants.width"
          && selected_names.design.units[1]
                 .concurrent_statements.front()
                 .value.text
              == "constants.next_value",
      "two- and three-part selected package names survive HIR parsing");

  const auto invalid = parse_text(
      "invalid_package_constants.vhd",
      R"(
package invalid_constants is
  constant duplicate : natural := 1;
  constant duplicate : natural := 2;
  constant missing : natural;
  constant vector_value : bit_vector(3 downto 0) := "0000";
end package invalid_constants;
package body invalid_constants is
  constant missing : natural;
end package body invalid_constants;
)",
      Language::Vhdl2008);
  require(!invalid.ok(), "invalid package constants must fail");
  const auto has_code = [&](const std::string_view code) {
    return std::any_of(
        invalid.diagnostics.begin(),
        invalid.diagnostics.end(),
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      has_code("FSIM-VHDL-SEM-020")
          && has_code("FSIM-VHDL-PARSE-088")
          && !has_code("FSIM-VHDL-UNSUPPORTED-023"),
      "invalid package constants have targeted diagnostics");

  const auto body = parse_text(
      "package_body.vhd",
      R"(
package body unsupported is
  function identity(value : integer) return integer is
  begin
    return value;
  end function identity;
end package body unsupported;
entity recovered is
end entity recovered;
)",
      Language::Vhdl2008);
  require(
      body.ok()
          && body.design.units.size() == 2
          && body.design.units.front().name == "unsupported"
          && body.design.units.front().primary_name
              == "unsupported"
          && body.design.units.front().functions.size() == 1
          && body.design.units.front().functions.front().defined
          && body.design.units.back().name == "recovered",
      "bounded package function bodies retain HIR and recover at the "
      "outer end clause");
}

void test_ignored_initializers_are_rejected() {
  const auto vhdl = parse_text(
      "initializers.vhd",
      R"(
entity initializers is
  port (input : in std_logic := '0');
end entity;
architecture rtl of initializers is
  signal state : std_logic := '1';
begin
end architecture;
)",
      Language::Vhdl2008);
  require(!vhdl.ok(), "VHDL signal initializers must be rejected");
  bool signal_initializer = false;
  for (const auto& diagnostic : vhdl.diagnostics) {
    signal_initializer =
        signal_initializer
        || diagnostic.code == "FSIM-VHDL-UNSUPPORTED-012";
  }
  const auto* entity =
      vhdl.design.find(UnitKind::VhdlEntity, "initializers");
  require(
      entity != nullptr && entity->ports.size() == 1
          && entity->ports.front().default_value
          && entity->ports.front().default_value->kind
              == ExpressionKind::LogicLiteral,
      "VHDL input-port defaults must remain in entity HIR");
  require(
      signal_initializer,
      "VHDL signal initializer needs a targeted diagnostic");

  const auto invalid_port_default = parse_text(
      "invalid-port-default.vhd",
      R"(
entity invalid_port_default is
  port (output_value : out bit := '0');
end entity;
)",
      Language::Vhdl2008);
  require(
      !invalid_port_default.ok()
          && std::ranges::any_of(
              invalid_port_default.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-075";
              }),
      "non-input VHDL entity-port defaults need a targeted diagnostic");

  const auto sv = parse_text(
      "initializers.sv",
      R"(
module initializers(
  input logic input_value = 1'b0
);
  logic state = 1'b1;
endmodule
)",
      Language::SystemVerilog2017);
  require(!sv.ok(), "SV defaults and initializers must be rejected");
  bool port_default_sv = false;
  bool declaration_initializer = false;
  for (const auto& diagnostic : sv.diagnostics) {
    port_default_sv =
        port_default_sv
        || diagnostic.code == "FSIM-SV-UNSUPPORTED-010";
    declaration_initializer =
        declaration_initializer
        || diagnostic.code == "FSIM-SV-UNSUPPORTED-011";
  }
  require(port_default_sv, "SV port default needs a targeted diagnostic");
  require(
      declaration_initializer,
      "SV declaration initializer needs a targeted diagnostic");
}

void test_duplicate_declarations_are_rejected() {
  const auto vhdl = parse_text(
      "duplicates.vhd",
      R"(
entity duplicates is
  port (value : in std_logic; VALUE : out std_logic);
end entity;
architecture rtl of duplicates is
  signal state : std_logic;
  signal STATE : std_logic;
begin
end architecture;
)",
      Language::Vhdl2008);
  require(!vhdl.ok(), "VHDL duplicate declarations must be rejected");
  bool duplicate_port = false;
  bool duplicate_signal = false;
  for (const auto& diagnostic : vhdl.diagnostics) {
    duplicate_port =
        duplicate_port || diagnostic.code == "FSIM-VHDL-SEM-002";
    duplicate_signal =
        duplicate_signal || diagnostic.code == "FSIM-VHDL-SEM-003";
  }
  require(duplicate_port, "VHDL duplicate port diagnostic");
  require(duplicate_signal, "VHDL duplicate signal diagnostic");

  const auto sv = parse_text(
      "duplicates.sv",
      R"(
module duplicates(input logic value, input logic value);
  logic state;
  logic state;
endmodule
)",
      Language::SystemVerilog2017);
  require(!sv.ok(), "SV duplicate declarations must be rejected");
  duplicate_port = false;
  duplicate_signal = false;
  for (const auto& diagnostic : sv.diagnostics) {
    duplicate_port =
        duplicate_port || diagnostic.code == "FSIM-SV-SEM-003";
    duplicate_signal =
        duplicate_signal || diagnostic.code == "FSIM-SV-SEM-006";
  }
  require(duplicate_port, "SV duplicate port diagnostic");
  require(duplicate_signal, "SV duplicate signal diagnostic");
}

void test_systemverilog_timescale_context() {
  const auto result = parse_text(
      "timescale.sv",
      R"(`timescale 10ns/100ps
module timed;
  initial #2 $finish;
endmodule
)",
      Language::SystemVerilog2017);
  require(result.ok(), "a legal `timescale must be represented");
  require(result.design.units.size() == 1, "timescale module count");
  const auto& unit = result.design.units.front();
  require(unit.time_unit == "10ns", "module time unit");
  require(unit.time_precision == "100ps", "module time precision");
  require(
      unit.processes.size() == 1
          && unit.processes.front().statements.size() == 1,
      "timed initial process");
  const auto& delay = *unit.processes.front().statements.front().delay;
  require(delay.magnitude == 20 && delay.unit == "ns",
          "integer delay must inherit and scale by `timescale");

  const auto mid_module_directive = parse_text(
      "mid_module_timescale.sv",
      R"(`timescale 1ns/1ns
module first;
  `timescale 10ns/1ns
  initial #2 $finish;
endmodule
module second;
  initial #2 $finish;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      mid_module_directive.ok(),
      "a directive inside a module must affect only following modules");
  require(
      mid_module_directive.design.units.size() == 2,
      "mid-module timescale design unit count");
  const auto& first = mid_module_directive.design.units[0];
  const auto& second = mid_module_directive.design.units[1];
  require(
      first.time_unit == "1ns"
          && first.processes.front().statements.front().delay->magnitude == 2,
      "current module must retain the timescale active at its start");
  require(
      second.time_unit == "10ns"
          && second.processes.front().statements.front().delay->magnitude == 20,
      "mid-module directive must apply to a following module");

  const auto invalid_precision = parse_text(
      "bad_timescale.sv",
      "`timescale 1ps/1ns\nmodule bad; endmodule\n",
      Language::SystemVerilog2017);
  require(!invalid_precision.ok(), "coarse precision must be rejected");
  require(
      std::any_of(
          invalid_precision.diagnostics.begin(),
          invalid_precision.diagnostics.end(),
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-009";
          }),
      "coarse precision diagnostic");

}

void test_systemverilog_time_declarations() {
  const auto parsed = parse_text(
      "time-declarations.sv",
      R"(timeunit 10ns;
timeprecision 1ps;
module inherited_time;
  logic marker;
  initial begin
    #1.25 marker = 1'b1;
    #(2.5ps) marker = 1'b0;
  end
endmodule
module local_time;
  timeunit 1us / 10ns;
  logic marker;
  initial begin
    #1.25e-1 marker = 1'b1;
  end
endmodule
module local_precision;
  timeprecision 100ps;
  initial #1 $finish;
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "SystemVerilog time declarations must parse");
  require(
      parsed.design.units.size() == 3,
      "time declaration module count");
  const auto& inherited = parsed.design.units[0];
  require(
      inherited.time_unit == "10ns"
          && inherited.time_precision == "1ps",
      "compilation-unit time declarations attach to a module");
  const auto& inherited_delays =
      inherited.processes.front().statements;
  require(
      inherited_delays.size() == 2
          && inherited_delays[0].delay
          && inherited_delays[0].delay->magnitude == 50
          && inherited_delays[0].delay->divisor == 4
          && inherited_delays[0].delay->unit == "ns",
      "fractional delay retains an exact rational under inherited timeunit");
  require(
      inherited_delays[1].delay
          && inherited_delays[1].delay->magnitude == 5
          && inherited_delays[1].delay->divisor == 2
          && inherited_delays[1].delay->unit == "ps",
      "an explicit time-literal suffix overrides the module timeunit");

  const auto& local = parsed.design.units[1];
  require(
      local.time_unit == "1us"
          && local.time_precision == "10ns",
      "combined module-local timeunit/timeprecision overrides inheritance");
  const auto& local_delay =
      *local.processes.front().statements.front().delay;
  require(
      local_delay.magnitude == 1
          && local_delay.divisor == 8
          && local_delay.unit == "us",
      "scientific fractional delays remain exact in typed HIR");
  const auto& local_precision = parsed.design.units[2];
  require(
      local_precision.time_unit == "10ns"
          && local_precision.time_precision == "100ps",
      "a module-local timeprecision overrides inherited precision only");

  const auto directive_precedence = parse_text(
      "time-directive-precedence.sv",
      R"(`timescale 100ns/10ns
timeunit 1ns / 1ps;
`resetall
module declared_time_wins;
  initial #1 $finish;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      directive_precedence.ok()
          && directive_precedence.design.units.front().time_unit == "1ns"
          && directive_precedence.design.units.front().time_precision
              == "1ps"
          && directive_precedence.design.units.front()
                 .processes.front().statements.front().delay->magnitude
              == 1
          && directive_precedence.design.units.front()
                 .processes.front().statements.front().delay->unit
              == "ns",
      "declarations override directive context and survive `resetall");

  const auto malformed = parse_text(
      "bad-time-declarations.sv",
      R"(timeunit 2ns;
timeunit 1ns;
timeunit 10ns;
module bad;
  timeprecision 100ns;
  timeprecision 1ps;
  logic marker;
  timeunit 1ns;
  initial #0.00000000000000000001 marker = 1'b1;
endmodule
timeprecision 1ps;
)",
      Language::SystemVerilog2017);
  const auto has_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            malformed.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      !malformed.ok()
          && has_code("FSIM-SV-SEM-045")
          && has_code("FSIM-SV-SEM-046")
          && has_code("FSIM-SV-SEM-047")
          && has_code("FSIM-SV-SEM-048")
          && has_code("FSIM-SV-SEM-049"),
      "invalid, duplicate, late, coarse, and overflowing "
      "fractional time forms receive targeted diagnostics");

  const auto unitless = parse_text(
      "unitless-fractional.sv",
      "module bad; initial #1.5 $finish; endmodule\n",
      Language::SystemVerilog2017);
  require(
      !unitless.ok()
          && std::ranges::any_of(
              unitless.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-050";
              }),
      "a unitless fractional delay without time context is rejected");

  const auto verilog = parse_text(
      "verilog-timeunit.v",
      "timeunit 1ns; module bad; endmodule\n",
      Language::Verilog2005);
  require(
      !verilog.ok()
          && std::ranges::any_of(
              verilog.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-044";
              }),
      "time declarations are rejected in Verilog-2005");

  const auto verilog_suffix = parse_text(
      "verilog-time-suffix.v",
      "module bad; initial #1ns $finish; endmodule\n",
      Language::Verilog2005);
  require(
      !verilog_suffix.ok()
          && std::ranges::any_of(
              verilog_suffix.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-051";
              }),
      "explicit delay-unit suffixes are rejected in Verilog-2005");

  const auto verilog_identifier = parse_text(
      "verilog-timeunit-identifier.v",
      R"(module timeunit;
endmodule
module top;
  timeunit child();
endmodule
)",
      Language::Verilog2005);
  require(
      verilog_identifier.ok()
          && verilog_identifier.design.units[1].instances.size() == 1,
      "timeunit remains an ordinary Verilog-2005 identifier when its "
      "syntax is not a declaration");
}

void test_systemverilog_delay_triples() {
    const auto parsed = parse_text(
        "delay-triples.sv",
        R"(timeunit 1ns / 1ps;
module delay_triples;
  logic source;
  logic continuous_result;
  logic gate_result;
  wire listed_result;
  wire listed_inverted;
  event fired;

  assign #(
      1e-3:2e-3:3e-3,
      4ps:5ps:6ps,
      7fs:8fs:9fs) continuous_result = source;
  buf #(1ps:2ps:3ps, 4ps:5ps:6ps) (gate_result, source);
  assign (weak0, strong1) #(2ps, 3ps, 4ps)
      listed_result = source, listed_inverted = ~source;

  initial begin
    #(0.1:0.2:0.3) source = 1'b1;
    source = #(4ps:5ps:6ps) 1'b0;
    source <= #(7ps:8ps:9ps) 1'b1;
    ->> #(10ps:11ps:12ps) fired;
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(parsed.ok(), "SystemVerilog delay triples must parse");
    const auto& unit = parsed.design.units.front();
    require(
        unit.concurrent_statements.size() == 4,
        "continuous lists and gate delay triples are retained");
    require(
        unit.concurrent_statements[2].target.text == "listed_result"
            && unit.concurrent_statements[3].target.text
                == "listed_inverted"
            && unit.concurrent_statements[2].delay
            && unit.concurrent_statements[3].delay
            && unit.concurrent_statements[2].delay->magnitude == 2
            && unit.concurrent_statements[3].delay->magnitude == 2
            && unit.concurrent_statements[2].delay->additional_values.size() == 2
            && unit.concurrent_statements[3].delay->additional_values.size() == 2
            && unit.concurrent_statements[2].delay->additional_values[0].magnitude == 3
            && unit.concurrent_statements[3].delay->additional_values[0].magnitude == 3
            && unit.concurrent_statements[2].delay->additional_values[1].magnitude == 4
            && unit.concurrent_statements[3].delay->additional_values[1].magnitude == 4
            && unit.concurrent_statements[2].verilog_drive_strength
            && unit.concurrent_statements[3].verilog_drive_strength
            && unit.concurrent_statements[2].verilog_drive_strength->zero
                == VerilogStrength::Weak
            && unit.concurrent_statements[3].verilog_drive_strength->zero
                == VerilogStrength::Weak
            && unit.concurrent_statements[2].verilog_drive_strength->one
                == VerilogStrength::Strong
            && unit.concurrent_statements[3].verilog_drive_strength->one
                == VerilogStrength::Strong,
        "each net assignment retains the shared delay and strength prefix");
    const auto& process = unit.processes.front().statements;
    require(
        process.size() == 4
            && process[0].kind == StatementKind::Delay
            && process[0].statements.size() == 1
            && process[1].kind == StatementKind::Assignment
            && process[1].assignment_kind == AssignmentKind::Blocking
            && process[2].kind == StatementKind::Assignment
            && process[2].assignment_kind == AssignmentKind::NonBlocking
            && process[3].kind == StatementKind::EventTrigger
            && process[3].assignment_kind == AssignmentKind::NonBlocking,
        "procedural, assignment, and named-event triple forms");

    const auto verify =
        [](const Delay& delay,
            const std::array<std::uint64_t, 3>& magnitudes,
            const std::array<std::uint64_t, 3>& divisors,
            const std::string_view unit_name) {
            require(
                delay.minimum && delay.typical && delay.maximum,
                "all min:typ:max HIR branches are present");
            require(
                delay.minimum->magnitude == magnitudes[0]
                    && delay.typical->magnitude == magnitudes[1]
                    && delay.maximum->magnitude == magnitudes[2]
                    && delay.minimum->divisor == divisors[0]
                    && delay.typical->divisor == divisors[1]
                    && delay.maximum->divisor == divisors[2]
                    && delay.minimum->unit == unit_name
                    && delay.typical->unit == unit_name
                    && delay.maximum->unit == unit_name,
                "delay triple values retain exact magnitudes, divisors, and units");
            require(
                delay.magnitude == magnitudes[1]
                    && delay.divisor == divisors[1]
                    && delay.unit == unit_name,
                "typed HIR defaults a delay triple to its typical branch");
        };

    verify(
        *unit.concurrent_statements[0].delay,
        { 1, 1, 3 },
        { 1000, 500, 1000 },
        "ns");
    verify(
        *unit.concurrent_statements[1].delay,
        { 1, 2, 3 },
        { 1, 1, 1 },
        "ps");
    require(
        unit.concurrent_statements[0].delay->additional_values.size() == 2
            && unit.concurrent_statements[1].delay->additional_values.size()
                == 1,
        "continuous and gate rise/fall/turnoff delay-list arity");
    verify(
        unit.concurrent_statements[0].delay->additional_values[0],
        { 4, 5, 6 },
        { 1, 1, 1 },
        "ps");
    verify(
        unit.concurrent_statements[0].delay->additional_values[1],
        { 7, 8, 9 },
        { 1, 1, 1 },
        "fs");
    verify(
        unit.concurrent_statements[1].delay->additional_values[0],
        { 4, 5, 6 },
        { 1, 1, 1 },
        "ps");
    verify(*process[0].delay, { 1, 1, 3 }, { 10, 5, 10 }, "ns");
    verify(*process[1].delay, { 4, 5, 6 }, { 1, 1, 1 }, "ps");
    verify(*process[2].delay, { 7, 8, 9 }, { 1, 1, 1 }, "ps");
    verify(*process[3].delay, { 10, 11, 12 }, { 1, 1, 1 }, "ps");

    const auto parameterized = parse_text(
        "parameterized-delays.sv",
        R"(timeunit 10ps / 1ps;
module parameterized_delays #(
    parameter int RISE = 2,
    localparam int FALL = RISE + 1);
  logic source;
  wire result;
  wire #(RISE, FALL, RISE + 2) net_result;
  wire #2 initialized = source;
  assign #(RISE:FALL:RISE + 2, FALL) result = source;
  assign net_result = source;
  initial #(RISE + 1) source = 1'b1;
endmodule
)",
        Language::SystemVerilog2017);
    require(
        parameterized.ok(),
        "locally constant parameterized delay expressions must parse");
    const auto& parameterized_unit = parameterized.design.units.front();
    const auto explicit_driver = std::ranges::find_if(
        parameterized_unit.concurrent_statements,
        [](const Statement& statement) {
            return statement.target.text == "result";
        });
    require(
        explicit_driver
            != parameterized_unit.concurrent_statements.end(),
        "parameterized continuous driver is retained");
    const auto& continuous = *explicit_driver->delay;
    require(
        continuous.expression
            && continuous.expression->kind == ExpressionKind::Identifier
            && continuous.expression->text == "FALL"
            && continuous.magnitude == 10
            && continuous.unit == "ps"
            && continuous.minimum && continuous.minimum->expression
            && continuous.typical && continuous.typical->expression
            && continuous.maximum && continuous.maximum->expression
            && continuous.additional_values.size() == 1
            && continuous.additional_values.front().expression,
        "parameterized transition-delay HIR retains expressions and time scale");
    const auto& procedural = *parameterized_unit.processes.front().statements.front().delay;
    require(
        procedural.expression
            && procedural.expression->kind == ExpressionKind::Binary
            && procedural.expression->text == "+"
            && procedural.magnitude == 10
            && procedural.unit == "ps",
        "parameterized procedural-delay HIR retains its expression tree");
    const auto net_result = std::ranges::find_if(
        parameterized_unit.signals,
        [](const SignalDeclaration& signal) {
            return signal.name == "net_result";
        });
    const auto initialized = std::ranges::find_if(
        parameterized_unit.signals,
        [](const SignalDeclaration& signal) {
            return signal.name == "initialized";
        });
    require(
        net_result != parameterized_unit.signals.end()
            && net_result->net_delay
            && net_result->net_delay->expression
            && net_result->net_delay->additional_values.size() == 2
            && initialized != parameterized_unit.signals.end()
            && initialized->net_delay
            && initialized->net_delay->magnitude == 20
            && std::ranges::any_of(
                parameterized_unit.concurrent_statements,
                [](const Statement& statement) {
                    return statement.target.text == "initialized"
                        && statement.assignment_kind
                        == AssignmentKind::Continuous;
                }),
        "net-declaration delays and declaration assignments retain HIR");

    const auto invalid_net_delay = parse_text(
        "invalid-net-delay.sv",
        "module invalid_net_delay; logic #2 value; endmodule\n",
        Language::SystemVerilog2017);
    require(
        !invalid_net_delay.ok()
            && std::ranges::any_of(
                invalid_net_delay.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-110";
                }),
        "net-declaration delays reject variable data types");

    const auto malformed = parse_text(
        "bad-delay-triples.sv",
        R"(module bad_delay_triples;
  wire source;
  wire result;
  assign #(1, 2, 3, 4) result = source;
  assign result = source, ;
  buf #(1, 2, 3) (result, source);
  initial begin
    #1:2:3;
    #(1:2);
    #(1::3);
    #(1, 2);
    #(1,);
  end
endmodule
)",
        Language::SystemVerilog2017);
    const auto has_code =
        [&](const std::string_view code) {
            return std::ranges::any_of(
                malformed.diagnostics,
                [&](const Diagnostic& diagnostic) {
                    return diagnostic.code == code;
                });
        };
    require(
        !malformed.ok()
            && has_code("FSIM-SV-SEM-052")
            && has_code("FSIM-SV-SEM-053")
            && has_code("FSIM-SV-PARSE-134")
            && has_code("FSIM-SV-PARSE-135")
            && has_code("FSIM-SV-PARSE-009")
            && has_code("FSIM-SV-PARSE-023"),
        "malformed triples and illegal transition-delay lists are targeted");
}

void test_systemverilog_procedural_assignment_controls() {
  const auto parsed = parse_text(
      "assignment-controls.sv",
      R"(timeunit 1ns / 1ps;
module assignment_controls;
  logic clock;
  logic enable;
  logic source;
  logic result;

  initial begin
    result = #3 source;
    result <= #(1:2:3) source;
    result = @(posedge clock) source;
    result <= @(negedge clock or enable) source;
    result = @clock source;
    result <= @* source;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      parsed.ok(),
      "blocking and nonblocking procedural assignment controls parse");
  const auto& statements =
      parsed.design.units.front().processes.front().statements;
  require(
      statements.size() == 6,
      "all procedural assignment control forms are retained");
  require(
      statements[0].procedural_assignment_control
              == ProceduralAssignmentControl::Delay
          && statements[0].assignment_kind
              == AssignmentKind::Blocking
          && statements[0].delay
          && statements[0].delay->magnitude == 3,
      "blocking intra-assignment delay has typed HIR");
  require(
      statements[1].procedural_assignment_control
              == ProceduralAssignmentControl::Delay
          && statements[1].assignment_kind
              == AssignmentKind::NonBlocking
          && statements[1].delay
          && statements[1].delay->minimum
          && statements[1].delay->typical
          && statements[1].delay->maximum,
      "nonblocking min/typ/max assignment delay is retained");
  require(
      statements[2].procedural_assignment_control
              == ProceduralAssignmentControl::Event
          && statements[2].sensitivities.size() == 1
          && statements[2].sensitivities.front().edge
              == EdgeKind::Positive
          && statements[2].sensitivities.front().signal
              == "clock",
      "blocking edge event control has typed sensitivity HIR");
  require(
      statements[3].procedural_assignment_control
              == ProceduralAssignmentControl::Event
          && statements[3].assignment_kind
              == AssignmentKind::NonBlocking
          && statements[3].sensitivities.size() == 2
          && statements[3].sensitivities[0].edge
              == EdgeKind::Negative
          && statements[3].sensitivities[1].edge
              == EdgeKind::Any,
      "nonblocking event lists preserve source order and edge kinds");
  require(
      statements[4].sensitivities.size() == 1
          && statements[4].sensitivities.front().signal == "clock",
      "an unparenthesized scalar event expression is accepted");
  require(
      statements[5].sensitivities.size() == 1
          && statements[5].sensitivities.front().signal == "*",
      "wildcard assignment event control is retained for RHS inference");

  const auto has_code =
      [](const ParseResult& result, const std::string_view code) {
        return std::ranges::any_of(
            result.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  const auto empty = parse_text(
      "empty-assignment-event.sv",
      R"(module empty_assignment_event;
  logic source;
  logic result;
  initial result = @() source;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !empty.ok() && has_code(empty, "FSIM-SV-PARSE-136"),
      "an empty assignment event list has a stable diagnostic");

  const auto repeated = parse_text(
      "repeated-assignment-control.sv",
      R"(module repeated_assignment_control;
  logic clock;
  logic source;
  logic result;
  initial result <= #1 @(posedge clock) source;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !repeated.ok()
          && has_code(repeated, "FSIM-SV-SEM-054"),
      "a repeated assignment control has a stable diagnostic");

  const auto repeat_event = parse_text(
      "repeat-assignment-event.sv",
      R"(module repeat_assignment_event;
  logic clock;
  logic source;
  logic result;
  initial result <= repeat (2) @(posedge clock) source;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      repeat_event.ok()
          && repeat_event.design.units.front().processes.front()
                 .statements.front().procedural_assignment_repeat
          && repeat_event.design.units.front().processes.front()
                 .statements.front().loop_limit.text == "2"
          && repeat_event.design.units.front().processes.front()
                 .statements.front().sensitivities.front().edge
              == EdgeKind::Positive,
      "repeated NBA event control retains count and event metadata");

  const auto restricted = parse_text(
      "restricted-assignment-controls.sv",
      R"(module restricted_assignment_controls;
  logic clock;
  logic source;
  logic result;
  always_comb result = #1 source;
  final result = @(posedge clock) source;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !restricted.ok()
          && has_code(restricted, "FSIM-SV-SEM-012")
          && has_code(restricted, "FSIM-SV-SEM-032"),
      "assignment controls participate in procedural restriction checks");
}

} // namespace fsim::tests::frontend

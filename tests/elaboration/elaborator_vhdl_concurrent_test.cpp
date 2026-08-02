// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_concurrent_assignments() {
  const auto parsed = fsim::frontend::parse_text(
      "vhdl_guarded_assignments.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_guarded_assignments is
  port (enabled : in boolean; selector, source : in std_logic;
        simple_value, conditional_value, selected_value : out std_logic);
end entity;
architecture rtl of vhdl_guarded_assignments is begin
  simple_base: simple_value <= '1';
  conditional_base: conditional_value <= '1';
  selected_base: selected_value <= '1';
  scope: block (enabled) begin
    simple_drive: simple_value <= guarded transport source after 1 ns;
    conditional_drive: conditional_value <= guarded transport
      source after 1 ns when selector = '1' else null after 1 ns;
    selected_drive: with selector select
      selected_value <= guarded transport
        source after 1 ns when '1', null after 1 ns when others;
  end block scope;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "vhdl:work.vhdl_guarded_assignments(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  assert(elaborated.design);
  assert(elaborated.design->processes().size() == 7);
  const auto enabled = elaborated.design->find_signal("enabled");
  const auto selector = elaborated.design->find_signal("selector");
  const auto source = elaborated.design->find_signal("source");
  const auto simple = elaborated.design->find_signal("simple_value");
  const auto conditional =
      elaborated.design->find_signal("conditional_value");
  const auto selected = elaborated.design->find_signal("selected_value");
  assert(enabled && selector && source && simple && conditional && selected);
  auto interpreter = elaborated.design->create_interpreter();
  const auto deposit = [&](const fsim::runtime::simir::SignalId signal,
                           const std::string_view value) {
    interpreter->deposit_signal(
        signal, fsim::runtime::PackedLogic4::from_msb_string(value));
  };
  const auto expect = [&](const std::string_view simple_expected,
                          const std::string_view conditional_expected,
                          const std::string_view selected_expected) {
    (void)interpreter->run();
    assert(interpreter->signal_value(*simple).to_msb_string()
           == simple_expected);
    assert(interpreter->signal_value(*conditional).to_msb_string()
           == conditional_expected);
    assert(interpreter->signal_value(*selected).to_msb_string()
           == selected_expected);
  };
  deposit(*source, "0");
  deposit(*selector, "1");
  deposit(*enabled, "0");
  expect("1", "1", "1");
  deposit(*enabled, "1");
  expect("X", "X", "X");
  deposit(*selector, "0");
  expect("X", "1", "1");
  deposit(*enabled, "0");
  expect("1", "1", "1");

  const auto outside = fsim::frontend::parse_text(
      "guarded_outside_block.vhd",
      "library ieee; use ieee.std_logic_1164.all; "
      "entity guarded_outside is port (q : out std_logic); end; "
      "architecture rtl of guarded_outside is begin "
      "q <= guarded '1'; end architecture;",
      fsim::frontend::Language::Vhdl2008);
  assert(outside.ok());
  const auto rejected_outside = fsim::elaboration::elaborate(
      outside.design, "vhdl:work.guarded_outside(rtl)");
  assert(!rejected_outside.ok());
  assert(has_diagnostic(
      rejected_outside, "FSIM-ELAB-VHDLGUARD-001"));

  const auto wrong_target = fsim::frontend::parse_text(
      "guarded_wrong_target.vhd",
      "entity guarded_wrong is port (q : out boolean); end; "
      "architecture rtl of guarded_wrong is begin "
      "scope: block (true) begin q <= guarded true; end block scope; "
      "end architecture;",
      fsim::frontend::Language::Vhdl2008);
  assert(wrong_target.ok());
  const auto rejected_target = fsim::elaboration::elaborate(
      wrong_target.design, "vhdl:work.guarded_wrong(rtl)");
  assert(!rejected_target.ok());
  assert(has_diagnostic(
      rejected_target, "FSIM-ELAB-VHDLGUARD-002"));

  const auto scoped = fsim::frontend::parse_text(
      "vhdl_statement_scopes.vhd",
      R"(
entity vhdl_statement_scopes is
  port (enabled, selector : in boolean;
        q : out integer range 0 to 7);
end entity;
architecture rtl of vhdl_statement_scopes is begin
  worker: process
    variable local_value : integer range 0 to 7 := 0;
  begin
    scan: for lane in 0 to 0 loop
      choose: if enabled then
        dispatch: case selector is
          when false => selected: local_value := lane + 1;
          when true => alternate: local_value := lane + 2;
        end case dispatch;
      end if choose;
    end loop scan;
    q <= local_value;
    wait;
  end process worker;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(scoped.ok());
  const auto scoped_result = fsim::elaboration::elaborate(
      scoped.design, "vhdl:work.vhdl_statement_scopes(rtl)");
  assert(scoped_result.ok() && scoped_result.design);
  const auto& scoped_process = scoped_result.design->processes().front();
  assert(scoped_process.name == "vhdl_statement_scopes.worker");
  std::vector<std::string> retained_scopes;
  for (const auto& operation : scoped_process.operations) {
    if (const auto* point =
            fsim::runtime::simir::operation_get_if<
                fsim::runtime::simir::DebugPoint>(&operation)) {
      retained_scopes.push_back(point->scope);
    }
  }
  const auto has_scope = [&](const std::string_view suffix) {
    return std::ranges::any_of(
        retained_scopes,
        [&](const std::string& scope) {
          return scope.ends_with(suffix);
        });
  };
  assert(has_scope("vhdl_statement_scopes.worker"));
  assert(has_scope("worker.scan"));
  assert(has_scope("worker.scan.choose"));
  assert(has_scope("worker.scan.choose.dispatch"));
  assert(std::ranges::any_of(
      retained_scopes,
      [](const std::string& scope) {
        return scope.find("worker.scan.choose.dispatch.$when_")
                   != std::string::npos
            && (scope.ends_with(".selected")
                || scope.ends_with(".alternate"));
      }));
  std::vector<fsim::runtime::simir::ExecutionPoint> execution_points;
  auto scoped_interpreter = scoped_result.design->create_interpreter();
  const auto scoped_enabled =
      scoped_result.design->find_signal("enabled");
  assert(scoped_enabled);
  scoped_interpreter->deposit_signal(
      *scoped_enabled,
      fsim::runtime::PackedLogic4::from_msb_string("1"));
  scoped_interpreter->set_execution_point_hook(
      [&](fsim::runtime::Scheduler&,
          const fsim::runtime::simir::ExecutionPoint& point) {
        execution_points.push_back(point);
      });
  (void)scoped_interpreter->run();
  assert(std::ranges::any_of(
      execution_points,
      [](const fsim::runtime::simir::ExecutionPoint& point) {
        return point.scope.find("worker.scan.choose.dispatch.$when_")
            != std::string::npos;
      }));
}

}  // namespace fsim::tests::elaboration

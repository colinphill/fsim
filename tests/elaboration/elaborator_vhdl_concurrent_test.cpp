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
  scope: block (enabled) is
    disconnect simple_value : std_logic after 2 ns;
  begin
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
  const auto simple_driver_count = std::ranges::count_if(
      elaborated.design->processes(),
      [&](const auto& process) {
        return std::ranges::any_of(
            process.driver_regions,
            [&](const auto& region) {
              return region.signal == *simple;
            });
      });
  assert(simple_driver_count == 2);
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
  const auto disconnect_start = interpreter->scheduler().now();
  deposit(*enabled, "0");
  const auto before_disconnect = interpreter->run(disconnect_start + 1);
  assert(before_disconnect.status == fsim::runtime::RunStatus::time_limit);
  assert(interpreter->signal_value(*simple).to_msb_string() == "X");
  deposit(*enabled, "1");
  assert(interpreter->run().status
         == fsim::runtime::RunStatus::completed);
  assert(interpreter->signal_value(*simple).to_msb_string() == "X");
  const auto restarted_disconnect = interpreter->scheduler().now();
  deposit(*enabled, "0");
  const auto before_restarted_disconnect =
      interpreter->run(restarted_disconnect + 1);
  assert(before_restarted_disconnect.status
         == fsim::runtime::RunStatus::time_limit);
  assert(interpreter->signal_value(*simple).to_msb_string() == "X");
  const auto after_disconnect = interpreter->run();
  assert(after_disconnect.status == fsim::runtime::RunStatus::completed);
  assert(interpreter->signal_value(*simple).to_msb_string() == "1");
  assert(interpreter->signal_value(*conditional).to_msb_string() == "1");
  assert(interpreter->signal_value(*selected).to_msb_string() == "1");

  {
    auto pending = elaborated.design->create_interpreter();
    pending->deposit_signal(
        *source,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    pending->deposit_signal(
        *enabled,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    assert(pending->run().status
           == fsim::runtime::RunStatus::completed);
    const auto pending_start = pending->scheduler().now();
    pending->deposit_signal(
        *enabled,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    assert(pending->run(pending_start + 1).status
           == fsim::runtime::RunStatus::time_limit);
  }
  auto isolated = elaborated.design->create_interpreter();
  assert(isolated->run().status
         == fsim::runtime::RunStatus::completed);
  assert(isolated->signal_value(*simple).to_msb_string() == "1");

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

  const auto oscillating = fsim::frontend::parse_text(
      "vhdl_concurrent_nonconvergence.vhd",
      "entity oscillating is end; "
      "architecture rtl of oscillating is signal q : boolean; begin "
      "q <= not q; end architecture;",
      fsim::frontend::Language::Vhdl2008);
  assert(oscillating.ok());
  const auto oscillating_result = fsim::elaboration::elaborate(
      oscillating.design, "vhdl:work.oscillating(rtl)");
  assert(oscillating_result.ok() && oscillating_result.design);
  auto oscillating_runtime = oscillating_result.design->create_interpreter(
      fsim::runtime::SchedulerOptions{8, 4});
  bool bounded_nonconvergence = false;
  try {
    (void)oscillating_runtime->run();
  } catch (const fsim::runtime::DeltaCycleLimitError& error) {
    bounded_nonconvergence = error.time() == 0
        && error.limit() == 8
        && !error.pending_orders().empty();
  }
  assert(bounded_nonconvergence);

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

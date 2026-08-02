// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_discrete_case_choices() {
  const auto positive = fsim::frontend::parse_text(
      "vhdl_discrete_case.vhd",
      R"(
entity vhdl_discrete_case is
  port (observed : out integer);
end entity;
architecture rtl of vhdl_discrete_case is
  type state_t is (idle, ready, done);
begin
  process
    variable selector : integer := 4;
    variable value : integer := 0;
    variable enabled : boolean := false;
    variable state : state_t := ready;
  begin
    case selector is
      when 0 | 1 to 3 => value := 1;
      when 6 downto 4 | 8 to 7 => value := 9;
      when others => value := 2;
    end case;
    with selector select
      value := 10 when 0 to 3, value when others;
    case enabled is
      when false => null;
      when true => null;
    end case;
    case state is
      when idle to ready => null;
      when done => null;
    end case;
    observed <= value;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(positive.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      positive.design, "vhdl:work.vhdl_discrete_case(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  assert(elaborated.design);
  const auto observed = elaborated.design->find_signal("observed");
  assert(observed);
  auto interpreter = elaborated.design->create_interpreter();
  (void)interpreter->run();
  assert(interpreter->signal_value(*observed).to_msb_string()
         == "00000000000000000000000000001001");

  const auto invalid = fsim::frontend::parse_text(
      "vhdl_invalid_case.vhd",
      R"(
entity vhdl_invalid_case is end entity;
architecture rtl of vhdl_invalid_case is
  subtype small_t is integer range 0 to 3;
begin
  process
    variable selector : integer := 1;
    variable small : small_t := 0;
    variable enabled : boolean := false;
    variable bound : integer := 2;
  begin
    case selector is
      when 0 to 2 => null;
      when 2 to 3 => null;
      when others => null;
    end case;
    case selector is
      when 1 | 1 => null;
      when others => null;
    end case;
    case small is
      when -1 => null;
      when others => null;
    end case;
    case enabled is
      when true => null;
    end case;
    case enabled is
      when 0 => null;
      when others => null;
    end case;
    case selector is
      when 0 to bound => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(invalid.ok());
  const auto rejected = fsim::elaboration::elaborate(
      invalid.design, "vhdl:work.vhdl_invalid_case(rtl)");
  assert(!rejected.ok());
  assert(has_diagnostic(rejected, "FSIM-ELAB-VHDLCASE-001"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-VHDLCASE-002"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-VHDLCASE-003"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-VHDLCASE-004"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-VHDLCASE-005"));
}

}  // namespace fsim::tests::elaboration

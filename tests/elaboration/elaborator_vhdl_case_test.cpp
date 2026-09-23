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
  const auto elaborated = compile_and_elaborate(
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
  const auto rejected = compile_and_elaborate(
      invalid.design, "vhdl:work.vhdl_invalid_case(rtl)");
  assert(!rejected.ok());
  assert(has_diagnostic(rejected, "FSIM-ELAB-VHDLCASE-001"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-VHDLCASE-002"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-VHDLCASE-003"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-VHDLCASE-004"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-VHDLCASE-005"));

  const auto packed_vector = fsim::frontend::parse_text(
      "vhdl_packed_vector_case.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_packed_vector_case is
  port (observed : out integer);
end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of vhdl_packed_vector_case is
  subtype state_t is std_logic_vector(2 downto 0);
  constant state_0 : state_t := "000";
  constant state_1 : state_t := "001";
  constant state_2 : state_t := "010";
  constant state_3 : state_t := "011";
  constant state_4 : state_t := "100";
begin
  process
    variable selector : state_t := state_4;
    variable value : integer := 0;
  begin
    case selector is
      when state_0 => value := 0;
      when state_1 => value := 1;
      when state_2 => value := 2;
      when state_3 => value := 3;
      when state_4 => value := 4;
      when others => value := -1;
    end case;
    observed <= value;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_vector.ok());
  const auto packed_elaborated = compile_and_elaborate(
      packed_vector.design, "vhdl:work.vhdl_packed_vector_case(rtl)");
  if (!packed_elaborated.ok()) {
    for (const auto& diagnostic : packed_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(packed_elaborated.ok());
  auto packed_interpreter = packed_elaborated.design->create_interpreter();
  (void)packed_interpreter->run();
  const auto packed_observed
      = packed_elaborated.design->find_signal("observed");
  assert(packed_observed);
  assert(packed_interpreter->signal_value(*packed_observed).to_msb_string()
         == "00000000000000000000000000000100");

  const auto packed_width_mismatch = fsim::frontend::parse_text(
      "vhdl_packed_vector_case_width.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_packed_vector_case_width is end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of vhdl_packed_vector_case_width is
  subtype state_t is std_logic_vector(2 downto 0);
  constant too_wide : std_logic_vector(3 downto 0) := "1000";
begin
  process
    variable selector : state_t := "000";
  begin
    case selector is
      when too_wide => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_width_mismatch.ok());
  const auto packed_width_rejected = compile_and_elaborate(
      packed_width_mismatch.design,
      "vhdl:work.vhdl_packed_vector_case_width(rtl)");
  assert(!packed_width_rejected.ok());
  assert(has_diagnostic(
      packed_width_rejected, "FSIM-ELAB-VHDLCASE-002"));
}

}  // namespace fsim::tests::elaboration

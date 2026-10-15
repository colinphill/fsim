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

  const auto packed_array_choices = fsim::frontend::parse_text(
      "vhdl_packed_array_case_choices.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity vhdl_packed_array_case_choices is
  port (observed : out integer);
end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
architecture rtl of vhdl_packed_array_case_choices is
  constant descending_bits : bit_vector(15 downto 8) := "11110000";
  constant ascending_bits : bit_vector(0 to 7) := "11001100";
  constant case_prefix : bit_vector(3 downto 0) := X"2";
  function source_bounds_preserved(value : unsigned) return boolean is
  begin
    return value'left = 10 and value'right = 3;
  end function;
begin
  process
    variable logic_selector : std_logic_vector(5 downto 3) := "010";
    variable bit_selector : bit_vector(5 downto 2) := "1100";
    variable reverse_selector : bit_vector(3 downto 0) := "1100";
    variable numeric_source : std_logic_vector(10 downto 3) := "00101010";
    variable meta_selector : std_logic_vector(31 downto 0) :=
      "LHH-HWUL-LHZ0UHH01WXXWXUUZX-WXUU";
    variable aggregate_selector : bit_vector(7 downto 0) := X"25";
    variable value : integer := 0;
  begin
    assert source_bounds_preserved(unsigned(numeric_source))
      report "numeric_std conversion did not preserve operand bounds"
      severity failure;
    case logic_selector is
      when "010" => value := value + 1;
      when others => value := -1;
    end case;
    case bit_selector is
      when descending_bits(13 downto 10) => value := value + 10;
      when others => value := -1;
    end case;
    case reverse_selector is
      when ascending_bits(4 to 7) => value := value + 100;
      when others => value := -1;
    end case;
    case unsigned(numeric_source) is
      when to_unsigned(42, 8) => value := value + 1000;
      when others => value := -1;
    end case;
    case signed(numeric_source) is
      when to_signed(42, 8) => value := value + 100000;
      when others => value := -1;
    end case;
    case meta_selector is
      when "LHH-HWUL-LHZ0UHH01WXXWXUUZX-WXUU" =>
        value := value + 10000;
      when others => value := -1;
    end case;
    case aggregate_selector is
      when (case_prefix, X"5") => value := value + 1000000;
      when others => value := -1;
    end case;
    observed <= value;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_array_choices.ok());
  const auto packed_array_elaborated = compile_and_elaborate(
      packed_array_choices.design,
      "vhdl:work.vhdl_packed_array_case_choices(rtl)");
  if (!packed_array_elaborated.ok()) {
    for (const auto& diagnostic : packed_array_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(packed_array_elaborated.ok());
  const auto packed_array_observed
      = packed_array_elaborated.design->find_signal("observed");
  assert(packed_array_observed);
  auto packed_array_interpreter
      = packed_array_elaborated.design->create_interpreter();
  (void)packed_array_interpreter->run();
  assert(packed_array_interpreter->signal_value(*packed_array_observed)
             .to_msb_string()
         == "00000000000100001111010001000111");

  const auto same_spelled_callable_unsigned = fsim::frontend::parse_text(
      "vhdl_packed_array_case_callable_unsigned.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity vhdl_packed_array_case_callable_unsigned is
  port (observed : out integer);
end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
architecture rtl of vhdl_packed_array_case_callable_unsigned is
  function unsigned(value : integer) return integer is
  begin
    return value;
  end function;
begin
  process
  begin
    case unsigned(7) is
      when 7 => observed <= 1;
      when others => observed <= 0;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(same_spelled_callable_unsigned.ok());
  const auto same_spelled_callable_unsigned_elaborated = compile_and_elaborate(
      same_spelled_callable_unsigned.design,
      "vhdl:work.vhdl_packed_array_case_callable_unsigned(rtl)");
  if (!same_spelled_callable_unsigned_elaborated.ok()) {
    for (const auto& diagnostic :
         same_spelled_callable_unsigned_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(same_spelled_callable_unsigned_elaborated.ok());
  const auto same_spelled_callable_observed
      = same_spelled_callable_unsigned_elaborated.design->find_signal(
          "observed");
  assert(same_spelled_callable_observed);
  auto same_spelled_callable_interpreter
      = same_spelled_callable_unsigned_elaborated.design
            ->create_interpreter();
  (void)same_spelled_callable_interpreter->run();
  assert(same_spelled_callable_interpreter->signal_value(
             *same_spelled_callable_observed)
             .to_msb_string()
         == "00000000000000000000000000000001");

  const auto exhaustive_bit_vector_case = fsim::frontend::parse_text(
      "vhdl_exhaustive_bit_vector_case.vhd",
      R"(
entity vhdl_exhaustive_bit_vector_case is
  port (observed : out integer);
end entity;
architecture rtl of vhdl_exhaustive_bit_vector_case is
begin
  process
    variable selector : bit_vector(0 to 0) := "1";
    variable result : integer := 0;
  begin
    case selector is
      when "0" => result := 0;
      when "1" => result := 42;
    end case;
    observed <= result;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(exhaustive_bit_vector_case.ok());
  const auto exhaustive_bit_vector_elaborated = compile_and_elaborate(
      exhaustive_bit_vector_case.design,
      "vhdl:work.vhdl_exhaustive_bit_vector_case(rtl)");
  if (!exhaustive_bit_vector_elaborated.ok()) {
    for (const auto& diagnostic :
         exhaustive_bit_vector_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(exhaustive_bit_vector_elaborated.ok());
  const auto exhaustive_bit_vector_observed
      = exhaustive_bit_vector_elaborated.design->find_signal("observed");
  assert(exhaustive_bit_vector_observed);
  auto exhaustive_bit_vector_interpreter
      = exhaustive_bit_vector_elaborated.design->create_interpreter();
  (void)exhaustive_bit_vector_interpreter->run();
  assert(exhaustive_bit_vector_interpreter
             ->signal_value(*exhaustive_bit_vector_observed)
             .to_msb_string()
         == "00000000000000000000000000101010");

  const auto exhaustive_logic_vector_case = fsim::frontend::parse_text(
      "vhdl_exhaustive_logic_vector_case.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_exhaustive_logic_vector_case is
  port (observed : out integer);
end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of vhdl_exhaustive_logic_vector_case is
begin
  process
    variable selector : std_logic_vector(0 to 0) := "H";
    variable result : integer := 0;
  begin
    case selector is
      when "U" => result := 1;
      when "X" => result := 2;
      when "0" => result := 3;
      when "1" => result := 4;
      when "Z" => result := 5;
      when "W" => result := 6;
      when "L" => result := 7;
      when "H" => result := 42;
      when "-" => result := 9;
    end case;
    observed <= result;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(exhaustive_logic_vector_case.ok());
  const auto exhaustive_logic_vector_elaborated = compile_and_elaborate(
      exhaustive_logic_vector_case.design,
      "vhdl:work.vhdl_exhaustive_logic_vector_case(rtl)");
  if (!exhaustive_logic_vector_elaborated.ok()) {
    for (const auto& diagnostic :
         exhaustive_logic_vector_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(exhaustive_logic_vector_elaborated.ok());
  const auto exhaustive_logic_vector_observed
      = exhaustive_logic_vector_elaborated.design->find_signal("observed");
  assert(exhaustive_logic_vector_observed);
  auto exhaustive_logic_vector_interpreter
      = exhaustive_logic_vector_elaborated.design->create_interpreter();
  (void)exhaustive_logic_vector_interpreter->run();
  assert(exhaustive_logic_vector_interpreter
             ->signal_value(*exhaustive_logic_vector_observed)
             .to_msb_string()
         == "00000000000000000000000000101010");

  const auto incomplete_bit_vector_case = fsim::frontend::parse_text(
      "vhdl_incomplete_bit_vector_case.vhd",
      R"(
entity vhdl_incomplete_bit_vector_case is end entity;
architecture rtl of vhdl_incomplete_bit_vector_case is
begin
  process
    variable selector : bit_vector(0 to 0) := "0";
  begin
    case selector is
      when "0" => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(incomplete_bit_vector_case.ok());
  const auto incomplete_bit_vector_rejected = compile_and_elaborate(
      incomplete_bit_vector_case.design,
      "vhdl:work.vhdl_incomplete_bit_vector_case(rtl)");
  assert(!incomplete_bit_vector_rejected.ok());
  assert(has_diagnostic(
      incomplete_bit_vector_rejected, "FSIM-ELAB-VHDLCASE-005"));

  const auto packed_array_range_choice = fsim::frontend::parse_text(
      "vhdl_packed_array_case_range_choice.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_packed_array_case_range_choice is end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of vhdl_packed_array_case_range_choice is
begin
  process
    variable selector : std_logic_vector(1 downto 0) := "01";
  begin
    case selector is
      when "00" to "11" => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_array_range_choice.ok());
  const auto packed_array_range_rejected = compile_and_elaborate(
      packed_array_range_choice.design,
      "vhdl:work.vhdl_packed_array_case_range_choice(rtl)");
  assert(!packed_array_range_rejected.ok());
  assert(has_diagnostic(
      packed_array_range_rejected, "FSIM-ELAB-VHDLCASE-001"));

  const auto packed_array_nominal_mismatch = fsim::frontend::parse_text(
      "vhdl_packed_array_case_nominal_mismatch.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_packed_array_case_nominal_mismatch is end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of vhdl_packed_array_case_nominal_mismatch is
  type custom_logic_vector is array (natural range <>) of std_ulogic;
  constant differently_typed : custom_logic_vector(2 downto 0) := "010";
begin
  process
    variable selector : std_logic_vector(2 downto 0) := "010";
  begin
    case selector is
      when differently_typed => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_array_nominal_mismatch.ok());
  const auto packed_array_nominal_rejected = compile_and_elaborate(
      packed_array_nominal_mismatch.design,
      "vhdl:work.vhdl_packed_array_case_nominal_mismatch(rtl)");
  assert(!packed_array_nominal_rejected.ok());
  assert(has_diagnostic(
      packed_array_nominal_rejected, "FSIM-ELAB-VHDLCASE-001"));

  const auto packed_array_dynamic_choice = fsim::frontend::parse_text(
      "vhdl_packed_array_case_dynamic_choice.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_packed_array_case_dynamic_choice is end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of vhdl_packed_array_case_dynamic_choice is
begin
  process
    variable selector : std_logic_vector(2 downto 0) := "010";
    variable runtime_choice : std_logic_vector(2 downto 0) := "010";
  begin
    case selector is
      when runtime_choice => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_array_dynamic_choice.ok());
  const auto packed_array_dynamic_rejected = compile_and_elaborate(
      packed_array_dynamic_choice.design,
      "vhdl:work.vhdl_packed_array_case_dynamic_choice(rtl)");
  assert(!packed_array_dynamic_rejected.ok());
  assert(has_diagnostic(
      packed_array_dynamic_rejected, "FSIM-ELAB-VHDLCASE-001"));

  const auto packed_array_user_function_choice = fsim::frontend::parse_text(
      "vhdl_packed_array_case_user_function.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_packed_array_case_user_function is end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of vhdl_packed_array_case_user_function is
  function user_defined_choice return std_logic_vector is
  begin
    return "010";
  end function;
begin
  process
    variable selector : std_logic_vector(2 downto 0) := "010";
  begin
    case selector is
      when user_defined_choice => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_array_user_function_choice.ok());
  const auto packed_array_user_function_rejected = compile_and_elaborate(
      packed_array_user_function_choice.design,
      "vhdl:work.vhdl_packed_array_case_user_function(rtl)");
  assert(!packed_array_user_function_rejected.ok());
  assert(has_diagnostic(
      packed_array_user_function_rejected, "FSIM-ELAB-VHDLCASE-001"));

  const auto numeric_std_user_unary_not_builtin
      = fsim::frontend::parse_text(
          "vhdl_numeric_std_user_unary_not_builtin_batch59.vhd",
          R"(
library ieee;
use ieee.numeric_std.all;
entity vhdl_numeric_std_user_unary_not_builtin_batch59 is
end entity;

library ieee;
use ieee.numeric_std.all;
architecture test of vhdl_numeric_std_user_unary_not_builtin_batch59 is
  type amount_t is range -10 to 10;
  function "-" (value : amount_t) return integer is
  begin
    return 5;
  end function;
  constant amount : amount_t := 1;
  signal selector : signed(7 downto 0);
begin
  check: process
  begin
    case selector is
      when to_signed(-amount, 8) => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(numeric_std_user_unary_not_builtin.ok());
  const auto numeric_std_user_unary_not_builtin_rejected
      = compile_and_elaborate(
          numeric_std_user_unary_not_builtin.design,
          "vhdl:work.vhdl_numeric_std_user_unary_not_builtin_batch59(test)");
  assert(!numeric_std_user_unary_not_builtin_rejected.ok());
  assert(has_diagnostic(
      numeric_std_user_unary_not_builtin_rejected,
      "FSIM-ELAB-VHDLCASE-001"));
  assert(!has_diagnostic(
      numeric_std_user_unary_not_builtin_rejected,
      "FSIM-ELAB-VHDLCASE-011"));

  const auto packed_array_shadow_to_unsigned = fsim::frontend::parse_text(
      "vhdl_packed_array_case_shadow_to_unsigned.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.unsigned;
entity vhdl_packed_array_case_shadow_to_unsigned is end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.unsigned;
architecture rtl of vhdl_packed_array_case_shadow_to_unsigned is
  function to_unsigned(value, size : natural) return integer is
  begin
    return value;
  end function;
begin
  process
    variable selector : unsigned(7 downto 0) := "00101010";
  begin
    case selector is
      when to_unsigned(42, 8) => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_array_shadow_to_unsigned.ok());
  const auto packed_array_shadow_to_unsigned_rejected = compile_and_elaborate(
      packed_array_shadow_to_unsigned.design,
      "vhdl:work.vhdl_packed_array_case_shadow_to_unsigned(rtl)");
  assert(!packed_array_shadow_to_unsigned_rejected.ok());
  assert(has_diagnostic(
      packed_array_shadow_to_unsigned_rejected,
      "FSIM-ELAB-VHDLCASE-001"));

  const auto packed_array_numeric_bit_conversion = fsim::frontend::parse_text(
      "vhdl_packed_array_case_numeric_bit_conversion.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.unsigned;
entity vhdl_packed_array_case_numeric_bit_conversion is end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.unsigned;
architecture rtl of vhdl_packed_array_case_numeric_bit_conversion is
begin
  process
    variable selector : unsigned(7 downto 0) := "00101010";
  begin
    case selector is
      when ieee.numeric_bit.to_unsigned(42, 8) => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_array_numeric_bit_conversion.ok());
  const auto packed_array_numeric_bit_conversion_rejected
      = compile_and_elaborate(
          packed_array_numeric_bit_conversion.design,
          "vhdl:work.vhdl_packed_array_case_numeric_bit_conversion(rtl)");
  assert(!packed_array_numeric_bit_conversion_rejected.ok());
  assert(has_diagnostic(
      packed_array_numeric_bit_conversion_rejected,
      "FSIM-ELAB-VHDLCASE-001"));

  const auto packed_array_dynamic_numeric_size = fsim::frontend::parse_text(
      "vhdl_packed_array_case_dynamic_numeric_size.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity vhdl_packed_array_case_dynamic_numeric_size is end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
architecture rtl of vhdl_packed_array_case_dynamic_numeric_size is
begin
  process
    variable selector : unsigned(7 downto 0) := "00101010";
    variable dynamic_size : natural := 8;
  begin
    case selector is
      when to_unsigned(42, dynamic_size) => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_array_dynamic_numeric_size.ok());
  const auto packed_array_dynamic_numeric_size_rejected = compile_and_elaborate(
      packed_array_dynamic_numeric_size.design,
      "vhdl:work.vhdl_packed_array_case_dynamic_numeric_size(rtl)");
  assert(!packed_array_dynamic_numeric_size_rejected.ok());
  assert(has_diagnostic(
      packed_array_dynamic_numeric_size_rejected,
      "FSIM-ELAB-VHDLCASE-001"));

  const auto packed_array_shadow_unsigned = fsim::frontend::parse_text(
      "vhdl_packed_array_case_shadow_unsigned.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_packed_array_case_shadow_unsigned is end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
architecture rtl of vhdl_packed_array_case_shadow_unsigned is
  type unsigned is array (natural range <>) of std_ulogic;
  type bit_vector is array (natural range <>) of bit;
  constant bit_choice : bit_vector(2 downto 0) := "010";
begin
  process
    variable selector : unsigned(2 downto 0) := "010";
  begin
    case selector is
      when bit_choice => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_array_shadow_unsigned.ok());
  const auto packed_array_shadow_unsigned_rejected = compile_and_elaborate(
      packed_array_shadow_unsigned.design,
      "vhdl:work.vhdl_packed_array_case_shadow_unsigned(rtl)");
  assert(!packed_array_shadow_unsigned_rejected.ok());
  assert(has_diagnostic(
      packed_array_shadow_unsigned_rejected, "FSIM-ELAB-VHDLCASE-001"));

  const auto packed_array_shadow_bit_vector = fsim::frontend::parse_text(
      "vhdl_packed_array_case_shadow_bit_vector.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_packed_array_case_shadow_bit_vector is end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of vhdl_packed_array_case_shadow_bit_vector is
  type unsigned is array (natural range <>) of std_ulogic;
  type bit_vector is array (natural range <>) of bit;
  constant unsigned_choice : unsigned(2 downto 0) := "010";
begin
  process
    variable selector : bit_vector(2 downto 0) := "010";
  begin
    case selector is
      when unsigned_choice => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_array_shadow_bit_vector.ok());
  const auto packed_array_shadow_bit_vector_rejected = compile_and_elaborate(
      packed_array_shadow_bit_vector.design,
      "vhdl:work.vhdl_packed_array_case_shadow_bit_vector(rtl)");
  assert(!packed_array_shadow_bit_vector_rejected.ok());
  assert(has_diagnostic(
      packed_array_shadow_bit_vector_rejected, "FSIM-ELAB-VHDLCASE-001"));

  const auto packed_array_shadow_unresolved_unsigned
      = fsim::frontend::parse_text(
          "vhdl_packed_array_case_shadow_unresolved_unsigned.vhd",
          R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_packed_array_case_shadow_unresolved_unsigned is end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
architecture rtl of vhdl_packed_array_case_shadow_unresolved_unsigned is
  type unresolved_unsigned is array (natural range <>) of std_ulogic;
  constant custom_choice : unresolved_unsigned(2 downto 0) := "010";
begin
  process
    variable selector : unsigned(2 downto 0) := "010";
  begin
    case selector is
      when custom_choice => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(packed_array_shadow_unresolved_unsigned.ok());
  const auto packed_array_shadow_unresolved_unsigned_rejected
      = compile_and_elaborate(
          packed_array_shadow_unresolved_unsigned.design,
          "vhdl:work.vhdl_packed_array_case_shadow_unresolved_unsigned(rtl)");
  assert(!packed_array_shadow_unresolved_unsigned_rejected.ok());
  assert(has_diagnostic(
      packed_array_shadow_unresolved_unsigned_rejected,
      "FSIM-ELAB-VHDLCASE-001"));

  const auto packed_array_duplicate = fsim::frontend::parse_text(
      "vhdl_packed_array_case_duplicate.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_packed_array_case_duplicate is end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of vhdl_packed_array_case_duplicate is
begin
  process
    variable selector : std_logic_vector(2 downto 0) := "010";
  begin
    case selector is
      when "010" => null;
      when "010" => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(packed_array_duplicate.ok());
  const auto packed_array_duplicate_rejected = compile_and_elaborate(
      packed_array_duplicate.design,
      "vhdl:work.vhdl_packed_array_case_duplicate(rtl)");
  assert(!packed_array_duplicate_rejected.ok());
  assert(has_diagnostic(
      packed_array_duplicate_rejected, "FSIM-ELAB-VHDLCASE-003"));

  const auto enum_loop_case = fsim::frontend::parse_text(
      "vhdl_enum_loop_case.vhd",
      R"(
entity vhdl_enum_loop_case is
  port (observed : out integer);
end entity;
architecture rtl of vhdl_enum_loop_case is
  type colors_t is (red, yellow, blue);
begin
  process
    variable value : integer := 0;
  begin
    for color in red to blue loop
      case color is
        when red => value := value + 1;
        when yellow => value := value + 10;
        when blue => value := value + 100;
      end case;
    end loop;
    observed <= value;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(enum_loop_case.ok());
  const auto enum_loop_case_elaborated = compile_and_elaborate(
      enum_loop_case.design, "vhdl:work.vhdl_enum_loop_case(rtl)");
  if (!enum_loop_case_elaborated.ok()) {
    for (const auto& diagnostic : enum_loop_case_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(enum_loop_case_elaborated.ok());
  const auto enum_loop_observed
      = enum_loop_case_elaborated.design->find_signal("observed");
  assert(enum_loop_observed);
  auto enum_loop_interpreter
      = enum_loop_case_elaborated.design->create_interpreter();
  (void)enum_loop_interpreter->run();
  assert(enum_loop_interpreter->signal_value(*enum_loop_observed)
             .to_msb_string()
         == "00000000000000000000000001101111");

  const auto enum_loop_foreign_choice = fsim::frontend::parse_text(
      "vhdl_enum_loop_foreign_choice.vhd",
      R"(
entity vhdl_enum_loop_foreign_choice is end entity;
architecture rtl of vhdl_enum_loop_foreign_choice is
  type colors_t is (red, yellow, blue);
  type modes_t is (idle, busy, blocked);
begin
  process
  begin
    for color in red to blue loop
      case color is
        when idle => null;
        when others => null;
      end case;
    end loop;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(enum_loop_foreign_choice.ok());
  const auto enum_loop_foreign_choice_rejected = compile_and_elaborate(
      enum_loop_foreign_choice.design,
      "vhdl:work.vhdl_enum_loop_foreign_choice(rtl)");
  if (enum_loop_foreign_choice_rejected.ok()
      || !has_diagnostic(
          enum_loop_foreign_choice_rejected, "FSIM-ELAB-VHDLCASE-001")) {
    for (const auto& diagnostic
         : enum_loop_foreign_choice_rejected.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(!enum_loop_foreign_choice_rejected.ok());
  assert(has_diagnostic(
      enum_loop_foreign_choice_rejected, "FSIM-ELAB-VHDLCASE-001"));

  const auto character_arrays = fsim::frontend::parse_text(
      "vhdl_character_array_case.vhd",
      R"(
entity vhdl_character_array_case is
  port (observed : out integer);
end entity;
architecture rtl of vhdl_character_array_case is
  type scal_t is array (1 to 2) of character;
  subtype string_4_t is string(1 to 4);
  type color_t is (red, yellow, blue);
begin
  process
    variable kk : scal_t := "TH";
    variable text : string_4_t := "1234";
    variable value : integer := 0;
  begin
    case kk is
      when "th" => value := -1;
      when "TH" => value := 5;
      when "AB" => value := 0;
      when others => value := 0;
    end case;
    case text is
      when "1234" => value := value + 10;
      when others => value := 0;
    end case;
    for c in red to blue loop
      case c is
        when red => value := value + 1;
        when yellow => value := value + 10;
        when blue => value := value + 100;
      end case;
    end loop;
    observed <= value;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(character_arrays.ok());
  const auto character_arrays_elaborated = compile_and_elaborate(
      character_arrays.design,
      "vhdl:work.vhdl_character_array_case(rtl)");
  if (!character_arrays_elaborated.ok()) {
    for (const auto& diagnostic : character_arrays_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(character_arrays_elaborated.ok());
  auto character_arrays_interpreter
      = character_arrays_elaborated.design->create_interpreter();
  (void)character_arrays_interpreter->run();
  const auto character_arrays_observed
      = character_arrays_elaborated.design->find_signal("observed");
  assert(character_arrays_observed);
  assert(character_arrays_interpreter
             ->signal_value(*character_arrays_observed).to_msb_string()
         == "00000000000000000000000001111110");

  const auto invalid_character_arrays = fsim::frontend::parse_text(
      "vhdl_invalid_character_array_case.vhd",
      R"(
entity vhdl_invalid_character_array_case is end entity;
architecture rtl of vhdl_invalid_character_array_case is
  type scal_t is array (1 to 2) of character;
  type other_t is array (1 to 2) of character;
  constant other_value : other_t := "TH";
begin
  process
    variable selector : scal_t := "TH";
  begin
    case selector is
      when "T" => null;
      when others => null;
    end case;
    case selector is
      when other_value => null;
      when others => null;
    end case;
    case selector is
      when "TH" => null;
      when "TH" => null;
      when others => null;
    end case;
    case selector is
      when "TH" => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(invalid_character_arrays.ok());
  const auto invalid_character_arrays_elaborated = compile_and_elaborate(
      invalid_character_arrays.design,
      "vhdl:work.vhdl_invalid_character_array_case(rtl)");
  assert(!invalid_character_arrays_elaborated.ok());
  assert(has_diagnostic(
      invalid_character_arrays_elaborated, "FSIM-ELAB-VHDLCASE-001"));
  assert(has_diagnostic(
      invalid_character_arrays_elaborated, "FSIM-ELAB-VHDLCASE-002"));
  assert(has_diagnostic(
      invalid_character_arrays_elaborated, "FSIM-ELAB-VHDLCASE-003"));
  assert(has_diagnostic(
      invalid_character_arrays_elaborated, "FSIM-ELAB-VHDLCASE-005"));

  const auto restricted_character_with_others = fsim::frontend::parse_text(
      "vhdl_restricted_character_case_with_others.vhd",
      R"(
entity vhdl_restricted_character_case_with_others is end entity;
architecture rtl of vhdl_restricted_character_case_with_others is
  subtype letter_t is character range 'A' to 'B';
  type restricted_t is array (1 to 1) of letter_t;
begin
  process
    variable selector : restricted_t := "A";
  begin
    case selector is
      when "A" => null;
      when "C" => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(restricted_character_with_others.ok());
  const auto restricted_with_others_rejected = compile_and_elaborate(
      restricted_character_with_others.design,
      "vhdl:work.vhdl_restricted_character_case_with_others(rtl)");
  assert(!restricted_with_others_rejected.ok());
  assert(has_diagnostic(
      restricted_with_others_rejected, "FSIM-ELAB-VHDLCASE-002"));

  const auto restricted_character_incomplete = fsim::frontend::parse_text(
      "vhdl_restricted_character_case_incomplete.vhd",
      R"(
entity vhdl_restricted_character_case_incomplete is end entity;
architecture rtl of vhdl_restricted_character_case_incomplete is
  subtype letter_t is character range 'A' to 'B';
  type restricted_t is array (1 to 1) of letter_t;
begin
  process
    variable selector : restricted_t := "A";
  begin
    case selector is
      when "A" => null;
      when "C" => null;
    end case;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(restricted_character_incomplete.ok());
  const auto restricted_incomplete_rejected = compile_and_elaborate(
      restricted_character_incomplete.design,
      "vhdl:work.vhdl_restricted_character_case_incomplete(rtl)");
  assert(!restricted_incomplete_rejected.ok());
  assert(has_diagnostic(
      restricted_incomplete_rejected, "FSIM-ELAB-VHDLCASE-002"));
  assert(has_diagnostic(
      restricted_incomplete_rejected, "FSIM-ELAB-VHDLCASE-005"));
  const auto incomplete_selected = fsim::frontend::parse_text(
      "vhdl_incomplete_selected_assignment.vhd",
      R"(
entity incomplete_selected is end entity;
architecture rtl of incomplete_selected is
  signal selector : bit_vector(1 downto 0) := "00";
  signal result : integer;
begin
  with selector select result <= 0 when "00", 1 when "01";
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(incomplete_selected.ok());
  const auto incomplete_selected_rejected = compile_and_elaborate(
      incomplete_selected.design,
      "vhdl:work.incomplete_selected(rtl)");
  assert(!incomplete_selected_rejected.ok());
  assert(has_diagnostic(
      incomplete_selected_rejected, "FSIM-ELAB-VHDLCASE-005"));

}

}  // namespace fsim::tests::elaboration

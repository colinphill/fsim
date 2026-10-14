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
}

}  // namespace fsim::tests::elaboration

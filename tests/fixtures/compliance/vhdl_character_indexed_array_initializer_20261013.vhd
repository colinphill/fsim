-- fsim: top=vhdl_character_indexed_array_initializer(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Test CHARACTER positions above BIT ordinals and a user enumeration with
-- identical character spellings but its own selected ordinal context.
entity vhdl_character_indexed_array_initializer is
end entity;

architecture sim of vhdl_character_indexed_array_initializer is
  type integer_by_character is array (character range <>) of integer;
  subtype digit_characters is integer_by_character('0' to '9');
  signal character_digits : digit_characters
    := (0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
begin
  process
  begin
    assert character_digits('0') = 0 and character_digits('9') = 9
      report "CHARACTER range did not use CHARACTER positions"
      severity failure;
    report "PASS";
    wait;
  end process;

  enum_control : entity work.vhdl_character_enum_index_control(sim);
end architecture;

entity vhdl_character_enum_index_control is
end entity;

architecture sim of vhdl_character_enum_index_control is
  type digit_index is ('0', '1', '2', '3', '4', '5', '6', '7', '8', '9');
  type integer_by_digit is array (digit_index range <>) of integer;
  subtype digit_ordinals is integer_by_digit('0' to '9');
  signal enum_digits : digit_ordinals
    := (10, 11, 12, 13, 14, 15, 16, 17, 18, 19);
begin
  process
  begin
    assert enum_digits('0') = 10 and enum_digits('9') = 19
      report "user enumeration range did not use its literal ordinals"
      severity failure;
    report "PASS";
    wait;
  end process;
end architecture;

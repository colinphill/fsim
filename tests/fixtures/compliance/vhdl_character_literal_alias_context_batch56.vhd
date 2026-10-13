-- fsim: top=vhdl_character_literal_alias_context_batch56(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Resolve CHARACTER through a subtype alias for scalar initializers while
-- preserving the ordinal identity of a user enum that shadows CHARACTER.
package vhdl_character_alias_context_pkg is
  subtype st_character is character;
  subtype scalar_character is st_character;
  subtype index_t is integer range 1 to 4;
  type character_array is array (index_t range <>) of st_character;
  subtype character_array_4 is character_array(index_t);
  constant character_z : scalar_character := 'Z';
  constant character_a : scalar_character := 'A';
  constant character_values : character_array_4 := (
    character_array_4'left | character_array_4'right => character_z,
    others => character_a);
end package;

entity vhdl_character_shadow_control_batch56 is
end entity;

architecture sim of vhdl_character_shadow_control_batch56 is
  type character is ('a', 'c');
  type shadow_index_array is array (character range <>) of integer;
  subtype shadow_index_array_2 is shadow_index_array('a' to 'c');
  constant shadow_values : shadow_index_array_2 := (10, 20);
begin
  process
  begin
    assert shadow_values'length = 2
       and shadow_values('a') = 10 and shadow_values('c') = 20
      report "user CHARACTER index bounds lost enumeration ordinals"
      severity failure;
    report "PASS";
    wait;
  end process;
end architecture;

entity vhdl_character_literal_alias_context_batch56 is
end entity;

use work.vhdl_character_alias_context_pkg.all;
architecture sim of vhdl_character_literal_alias_context_batch56 is
  subtype vests_index is integer range 1 to 8;
  subtype vests_scalar_character is character;
  type vests_character_array is array (vests_index range <>) of vests_scalar_character;
  subtype vests_character_array_8 is vests_character_array(vests_index);
  subtype vests_character_index is character range 'a' to 'd';
  type vests_integer_by_character_alias is array (
    vests_character_index range <>) of integer range 1 to integer'high;
  constant vests_character_low : vests_scalar_character := vests_scalar_character'left;
  constant vests_character_z : vests_scalar_character := 'Z';
  constant vests_character_low_array : vests_character_array_8 :=
    (others => vests_character_low);
  constant vests_character_z_array : vests_character_array_8 :=
    (vests_character_array_8'left | vests_character_array_8'right => vests_character_z,
     others => vests_character_low);
begin
  process
    constant local_unconstrained_copy : vests_character_array(vests_index) :=
      vests_character_z_array;
    variable local_constrained_copy : vests_character_array_8 :=
      vests_character_z_array;
  begin
    assert local_unconstrained_copy(1) = 'Z' and
           local_unconstrained_copy(8) = 'Z' and
           local_constrained_copy(1) = 'Z' and
           local_constrained_copy(8) = 'Z' and
           local_constrained_copy(2) = character'val(0) and
           local_unconstrained_copy(7) = character'val(0)
      report "local CHARACTER alias aggregate lost its literal or bounds"
      severity failure;
    report "PASS";
    wait;
  end process;

  process
    variable local_character_indexed_values :
      vests_integer_by_character_alias('a' to 'c');
  begin
    assert local_character_indexed_values'length = 3
      report "CHARACTER subtype alias did not size its indexed array"
      severity failure;
    report "PASS";
    wait;
  end process;

  process
    variable character_copy : character_array_4 := character_values;
  begin
    assert character_copy(1) = 'Z' and character_copy(4) = 'Z'
      report "predefined CHARACTER alias lost its position"
      severity failure;
    report "PASS";
    wait;
  end process;

  shadow_control : entity work.vhdl_character_shadow_control_batch56(sim);
end architecture;

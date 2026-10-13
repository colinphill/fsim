-- fsim: top=component_binding_character_generic_batch56(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- A character literal is a locally static scalar generic actual.
entity component_binding_character_leaf_batch56 is
  generic (character_value : character := 'a');
end entity;

architecture rtl of component_binding_character_leaf_batch56 is
begin
  check : process
  begin
    wait for 1 ns;
    assert character_value = '$'
      report "FAIL: character generic binding changed the literal"
      severity error;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;

entity component_binding_character_generic_batch56 is
end entity;

architecture sim of component_binding_character_generic_batch56 is
  component character_component_batch56 is
    generic (value : character);
  end component;

  for instance : character_component_batch56
    use entity work.component_binding_character_leaf_batch56(rtl)
      generic map (character_value => value);
begin
  instance : character_component_batch56
    generic map ('$');
end architecture;

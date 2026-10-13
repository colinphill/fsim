-- fsim: top=component_binding_generic_overlay_batch56(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 5.2.1.2: a component binding indication's generic map
-- actual may use the component generic value, including its default.
entity component_binding_generic_overlay_leaf_batch56 is
  generic (g1 : integer := 9);
  port (value_out : out integer);
end entity;

architecture rtl of component_binding_generic_overlay_leaf_batch56 is
begin
  value_out <= g1;
end architecture;

entity component_binding_generic_overlay_batch56 is
end entity;

architecture sim of component_binding_generic_overlay_batch56 is
  component generic_binding_component_batch56 is
    generic (g1 : integer := 2);
    port (value_out : out integer);
  end component;

  signal default_value : integer := 0;
  signal explicit_value : integer := 0;

  for default_instance : generic_binding_component_batch56
    use entity work.component_binding_generic_overlay_leaf_batch56(rtl)
      generic map (g1 => g1 + g1);
  for explicit_instance : generic_binding_component_batch56
    use entity work.component_binding_generic_overlay_leaf_batch56(rtl)
      generic map (g1 => g1 + g1);
begin
  default_instance : generic_binding_component_batch56
    port map (default_value);
  explicit_instance : generic_binding_component_batch56
    generic map (g1 => 5)
    port map (explicit_value);

  check : process
  begin
    wait for 1 ns;
    assert default_value = 4
      report "FAIL: binding map did not use component generic default"
      severity error;
    assert explicit_value = 10
      report "FAIL: binding map ignored explicit component generic actual"
      severity error;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;

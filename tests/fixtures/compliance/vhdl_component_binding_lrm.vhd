-- fsim: top=component_binding_lrm(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 5.2.1.2: default binding opens entity generics absent
-- from the component interface; an open generic with a default uses it.
entity binding_leaf is
  generic (g : integer := 23);
end entity;

architecture z_earlier of binding_leaf is
begin
  check_binding : process
  begin
    assert g = 23
      report "FAIL: open generic did not use its default" severity error;
    assert false
      report "FAIL: earlier architecture was selected" severity error;
    wait;
  end process;
end architecture z_earlier;

architecture a_latest of binding_leaf is
begin
  check_binding : process
  begin
    assert g = 23
      report "FAIL: open generic did not use its default" severity error;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture a_latest;

entity dormant_target is
end entity;

architecture unused of dormant_target is
begin
end architecture;

entity component_binding_lrm is
end entity;

architecture sim of component_binding_lrm is
  component binding_leaf
  end component;
  component dormant_component
  end component;
  for configured_binding : binding_leaf
    use entity work.binding_leaf;
  for all : dormant_component
    use entity work.dormant_target;
begin
  default_binding : binding_leaf;
  configured_binding : binding_leaf;
end architecture;

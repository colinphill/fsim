-- fsim: top=component_selected_default_batch56_cfg std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 7.3.2.1 and 7.3.3: package-selected component names are
-- retained, and an omitted binding with no same-name entity defaults OPEN.
package component_selected_default_pkg is
  component selected_leaf is
    port (result_out : out bit);
  end component;
end package;

entity component_selected_default_leaf is
  port (result_out : out bit);
end entity;

architecture implementation of component_selected_default_leaf is
begin
  result_out <= '1';
end architecture;

entity component_selected_default_batch56 is
end entity;

use work.component_selected_default_pkg;
architecture sim of component_selected_default_batch56 is
  component absent_component
  end component;
  signal observed : bit := '0';
begin
  selected : component component_selected_default_pkg.selected_leaf
    port map (observed);
  missing : component absent_component;

  check : process
  begin
    wait for 1 ns;
    assert observed = '1'
      report "FAIL: selected package component was not bound"
      severity error;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;

configuration component_selected_default_batch56_cfg
  of component_selected_default_batch56 is
  for sim
    for selected : component_selected_default_pkg.selected_leaf
      use entity work.component_selected_default_leaf(implementation);
    end for;
    for missing : absent_component
    end for;
  end for;
end configuration;

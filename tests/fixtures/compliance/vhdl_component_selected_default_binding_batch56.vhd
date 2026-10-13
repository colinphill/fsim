-- fsim: top=component_selected_default_binding_batch56(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 7.3.2.1 and 7.3.3: a package-selected component with no
-- explicit binding defaults to an entity named after the component itself.
package component_selected_default_binding_pkg is
  component component_selected_default_binding_leaf is
    port (result_out : out bit);
  end component;
end package;

entity component_selected_default_binding_leaf is
  port (result_out : out bit);
end entity;

architecture implementation of component_selected_default_binding_leaf is
begin
  result_out <= '1';
end architecture;

entity component_selected_default_binding_batch56 is
end entity;

use work.component_selected_default_binding_pkg;
architecture sim of component_selected_default_binding_batch56 is
  signal observed : bit := '0';
begin
  selected : component component_selected_default_binding_pkg.component_selected_default_binding_leaf
    port map (observed);

  check : process
  begin
    wait for 1 ns;
    assert observed = '1'
      report "FAIL: default-bound package component did not drive output"
      severity error;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;

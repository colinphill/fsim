-- fsim: top=nested_component_configuration_batch56_cfg std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 3.4.3: the block configuration nested under a component
-- binding configures instances in the selected architecture.
entity nested_configuration_leaf is
  port (result_out : out bit);
end entity;

architecture implementation of nested_configuration_leaf is
begin
  result_out <= '1';
end architecture;

entity nested_configuration_container is
  port (result_out : out bit);
end entity;

architecture structural of nested_configuration_container is
  component leaf_cell
    port (result_out : out bit);
  end component;
begin
  child : component leaf_cell port map (result_out);
end architecture;

entity nested_component_configuration_batch56 is
end entity;

architecture sim of nested_component_configuration_batch56 is
  component container_cell
    port (result_out : out bit);
  end component;
  signal observed : bit := '0';
begin
  configured : component container_cell port map (observed);

  check : process
  begin
    wait for 1 ns;
    assert observed = '1'
      report "FAIL: nested block configuration was not applied"
      severity error;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;

configuration nested_component_configuration_batch56_cfg
  of nested_component_configuration_batch56 is
  for sim
    for configured : container_cell
      use entity work.nested_configuration_container(structural);
      for structural
        for child : leaf_cell
          use entity work.nested_configuration_leaf(implementation);
        end for;
      end for;
    end for;
  end for;
end configuration;

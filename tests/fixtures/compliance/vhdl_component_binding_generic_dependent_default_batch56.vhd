-- fsim: top=component_binding_generic_dependent_default_batch56(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- The later component generic default reads the earlier generic's value.
entity component_binding_generic_dependent_default_leaf_batch56 is
  generic (
    seed_value : integer := 0;
    doubled_value : integer := 0;
    enabled_value : boolean := false
  );
  port (
    seed_out : out integer;
    doubled_out : out integer;
    enabled_out : out boolean
  );
end entity;

architecture rtl of component_binding_generic_dependent_default_leaf_batch56 is
begin
  seed_out <= seed_value;
  doubled_out <= doubled_value;
  enabled_out <= enabled_value;
end architecture;

entity component_binding_generic_dependent_default_batch56 is
end entity;

architecture sim of component_binding_generic_dependent_default_batch56 is
  component dependent_generic_component_batch56 is
    generic (
      seed : integer := 2;
      doubled : integer := seed + seed;
      enabled : boolean := true
    );
    port (
      seed_out : out integer;
      doubled_out : out integer;
      enabled_out : out boolean
    );
  end component;

  signal default_seed : integer := 0;
  signal default_doubled : integer := 0;
  signal default_enabled : boolean := false;
  signal override_seed : integer := 0;
  signal override_doubled : integer := 0;
  signal override_enabled : boolean := true;

  for default_instance : dependent_generic_component_batch56
    use entity work.component_binding_generic_dependent_default_leaf_batch56(rtl)
      generic map (
        seed_value => seed,
        doubled_value => doubled,
        enabled_value => enabled
      );
  for override_instance : dependent_generic_component_batch56
    use entity work.component_binding_generic_dependent_default_leaf_batch56(rtl)
      generic map (
        seed_value => seed,
        doubled_value => doubled,
        enabled_value => enabled
      );
begin
  default_instance : dependent_generic_component_batch56
    port map (default_seed, default_doubled, default_enabled);

  override_instance : dependent_generic_component_batch56
    generic map (seed => 5, enabled => false)
    port map (override_seed, override_doubled, override_enabled);

  check : process
  begin
    wait for 1 ns;
    assert default_seed = 2 and default_doubled = 4 and default_enabled
      report "FAIL: dependent defaults did not use declaration-order values"
      severity error;
    assert override_seed = 5 and override_doubled = 10
        and not override_enabled
      report "FAIL: explicit integer/Boolean component actuals were ignored"
      severity error;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;

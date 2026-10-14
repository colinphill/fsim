-- fsim: top=vhdl_block_generic_enclosing_scope_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Block generic actuals use the enclosing declarative region.
entity vhdl_block_generic_enclosing_scope_batch58 is
end entity;

architecture sim of vhdl_block_generic_enclosing_scope_batch58 is
  constant size : integer := 3;
  constant outer_size : integer := 5;
begin
  same_name: block is
    generic (size : integer);
    generic map (size => size);
  begin
    assert size = 3
      report "same-name block generic actual did not use outer constant"
      severity failure;
  end block same_name;

  compound: block is
    generic (size : integer);
    generic map (size => size + 1);
  begin
    assert size = 4
      report "compound block generic actual did not use outer constant"
      severity failure;
  end block compound;

  distinct_name: block is
    generic (size : integer);
    generic map (size => outer_size);
  begin
    assert size = 5
      report "distinct enclosing name was not imported"
      severity failure;
  end block distinct_name;
  completion : process is
  begin
    wait for 1 ns;
    report "PASS";
    wait;
  end process;
end architecture;

-- fsim: top=d8_block_configuration_local_generic(t) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 7.3.2.2, 7.3.2.3: the actuals of a binding indication's
-- maps may name the local generics and ports of the component, also for a
-- configuration specification in a block's declarative part.
entity d8_bcl_bottom is
  generic (gb : integer);
  port (pb : in integer);
end entity;
architecture a of d8_bcl_bottom is
begin
  process
  begin
    wait for 1 ns;
    assert gb = 5 severity failure;
    assert pb = 7 severity failure;
    if gb = 5 and pb = 7 then
      report "PASS";
    end if;
    wait;
  end process;
end architecture;

entity d8_block_configuration_local_generic is end entity;
architecture t of d8_block_configuration_local_generic is
  component bc is
    generic (gc : integer);
    port (pc : in integer);
  end component;
  signal s : integer := 7;
begin
  b1 : block
    for l1 : bc use entity work.d8_bcl_bottom(a)
      generic map (gb => gc)
      port map (pb => pc);
  begin
    l1 : bc generic map (5) port map (s);
  end block;
end architecture;

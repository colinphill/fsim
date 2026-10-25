-- fsim: top=vhdl_port_element_resolved(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 6.5.7.1, 14.7.3.2: a formal associated with a slice of a
-- resolved actual is a source of that slice only. The resolution function
-- combines it with the slice's other sources in the same simulation cycle as
-- the formal changes, and the actual's other elements keep their own
-- sources.
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_port_element_resolved_src is
  port (go : in std_logic;
        y : out std_logic_vector(1 downto 0));
end entity;

architecture rtl of vhdl_port_element_resolved_src is
begin
  y <= "10" when go = '1' else "01";
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity vhdl_port_element_resolved is
end entity;

architecture sim of vhdl_port_element_resolved is
  signal go : std_logic := '0';
  signal r : std_logic_vector(3 downto 0);
begin
  u : entity work.vhdl_port_element_resolved_src
    port map (go => go, y => r(1 downto 0));
  -- Second sources of r(1) and r(0); the only sources of r(3 downto 2).
  r(1) <= 'Z';
  r(0) <= '1';
  r(3 downto 2) <= "10";

  check : process
  begin
    wait for 1 ns;
    assert r = "1001"
      report "FAIL initial resolution " & to_string(r) severity failure;
    go <= '1';
    wait for 0 ns;
    -- Delta 1: go updated; the child's assignments are not yet effective.
    assert r = "1001"
      report "FAIL r changed with go " & to_string(r) severity failure;
    wait for 0 ns;
    -- Delta 2: y updated and, with it, the slice it drives.
    assert r = "101X"
      report "FAIL r did not resolve its port sources in the same delta "
        & to_string(r) severity failure;
    assert r'event report "FAIL r has no event" severity failure;
    wait for 0 ns;
    assert not r'event
      report "FAIL r changed again a delta later" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;

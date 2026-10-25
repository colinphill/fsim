-- fsim: top=vhdl_port_element_wiring(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 6.5.7.1, 14.7.3.2, 14.7.3.3: a formal associated with an
-- element of its actual has the element's effective value (in) or drives the
-- element (out) in the same simulation cycle, with no delta of its own. The
-- child's assignment is the only delta between s(i) and r(i).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_port_element_wiring_inc is
  port (a : in std_logic_vector(3 downto 0);
        y : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of vhdl_port_element_wiring_inc is
begin
  y <= std_logic_vector(unsigned(a) + 1);
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity vhdl_port_element_wiring is
end entity;

architecture sim of vhdl_port_element_wiring is
  type nibbles is array (0 to 2) of std_logic_vector(3 downto 0);
  signal s : nibbles := (others => x"0");
  signal r : nibbles;
begin
  lanes : for i in 0 to 2 generate
    u : entity work.vhdl_port_element_wiring_inc
      port map (a => s(i), y => r(i));
  end generate;

  check : process
  begin
    wait for 1 ns;
    assert r(0) = x"1" and r(1) = x"1" and r(2) = x"1"
      report "FAIL wired elements did not settle" severity failure;
    s(1) <= x"5";
    wait for 0 ns;
    -- Delta 1: s(1) and the child's formal a updated; y not yet.
    assert s(1) = x"5" report "FAIL s(1) not updated" severity failure;
    assert r(1) = x"1"
      report "FAIL r(1) changed in the delta of s(1)" severity failure;
    wait for 0 ns;
    -- Delta 2: the child's assignment updated y and, with it, r(1).
    assert r(1) = x"6"
      report "FAIL r(1) did not follow y in the same delta" severity failure;
    assert r(1)'event report "FAIL r(1) has no event" severity failure;
    assert r(0) = x"1" and r(2) = x"1"
      report "FAIL other elements changed" severity failure;
    wait for 0 ns;
    assert not r(1)'event
      report "FAIL r(1) changed again a delta later" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;

-- fsim: top=vhdl_port_conversion_slice(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 6.5.7.1, 14.7.3.2: an out formal with a conversion function
-- in its formal part, associated with a slice or an element of its actual,
-- is a source of that subelement only. The converted driving value reaches
-- the actual in the same simulation cycle as the formal changes, as for an
-- association with the whole actual.
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_port_conversion_slice_src is
  port (go : in std_logic;
        b : out bit_vector(1 downto 0);
        e : out bit);
end entity;

architecture rtl of vhdl_port_conversion_slice_src is
begin
  b <= "10" when go = '1' else "01";
  e <= '1' when go = '1' else '0';
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity vhdl_port_conversion_slice is
end entity;

architecture sim of vhdl_port_conversion_slice is
  signal go : std_logic := '0';
  -- Whole actual, the reference timing.
  signal w : std_logic_vector(1 downto 0);
  -- Slice of a resolved actual with other sources on its other elements.
  signal r : std_logic_vector(3 downto 0);
  -- Slice and element of an unresolved actual.
  signal u : std_ulogic_vector(3 downto 0);
begin
  whole : entity work.vhdl_port_conversion_slice_src
    port map (go => go, to_stdlogicvector(b) => w, e => open);
  resolved : entity work.vhdl_port_conversion_slice_src
    port map (go => go, to_stdlogicvector(b) => r(2 downto 1),
              to_stdulogic(e) => r(0));
  unresolved : entity work.vhdl_port_conversion_slice_src
    port map (go => go, to_stdulogicvector(b) => u(3 downto 2),
              to_stdulogic(e) => u(1));
  r(3) <= '1';
  u(0) <= '0';

  check : process
  begin
    wait for 1 ns;
    assert w = "01" and r = "1010" and u = "0100"
      report "FAIL initial values " & to_string(w) & " " & to_string(r)
        & " " & to_string(u) severity failure;
    go <= '1';
    wait for 0 ns;
    -- Delta 1: go updated; the formals are not yet updated.
    assert w = "01" and r = "1010" and u = "0100"
      report "FAIL actuals changed with go " & to_string(w) & " "
        & to_string(r) & " " & to_string(u) severity failure;
    wait for 0 ns;
    -- Delta 2: the formals updated and, with them, every actual.
    assert w = "10"
      report "FAIL whole actual " & to_string(w) severity failure;
    assert r = "1101"
      report "FAIL resolved slice lags its formal " & to_string(r)
        severity failure;
    assert u = "1010"
      report "FAIL unresolved slice lags its formal " & to_string(u)
        severity failure;
    assert r'event and u'event
      report "FAIL actuals have no event" severity failure;
    wait for 0 ns;
    assert not r'event and not u'event
      report "FAIL actuals changed again a delta later" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;

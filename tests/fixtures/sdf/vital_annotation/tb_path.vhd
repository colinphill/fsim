-- SPDX-License-Identifier: Apache-2.0
--
-- A buffer vbuf is driven 0 (the initial value), 1 at 10 ns and 0 at 30 ns.
-- VitalWireDelay delays the input by the element of tipd_a for the new value
-- (the first for a rising input, the second for a falling one; a change from
-- 'U' to '0' is a falling one), and VitalPathDelay01 delays the output by the
-- element of tpd_a_y for the new output value (IEEE 1076.4-2000 9.2, Table 7).
-- The two delays add.
--
-- path_30.sdf, typical: tipd_a = (1 ns, 2 ns), tpd_a_y = (3 ns, 4 ns).
--   a_ipd falls at 0 + 2 = 2 ns and y falls at 2 + 4 = 6 ns;
--   a rises at 10 ns, a_ipd rises at 11 ns, y rises at 11 + 3 = 14 ns;
--   a falls at 30 ns, a_ipd falls at 32 ns, y falls at 32 + 4 = 36 ns.
-- path_30.sdf, maximum: tipd_a = (1.5 ns, 3 ns), tpd_a_y = (4 ns, 5 ns).
--   y falls at 3 + 5 = 8 ns, rises at 10 + 1.5 + 4 = 15.5 ns and falls at
--   30 + 3 + 5 = 38 ns.
-- path_21.sdf: the typical values of path_30.sdf.
-- unsupported_40.sdf has no effect on the generics, which keep their defaults
-- tipd_a = (0.5 ns, 0.7 ns) and tpd_a_y = (1 ns, 2 ns):
--   y falls at 0.7 + 2 = 2.7 ns, rises at 10 + 0.5 + 1 = 11.5 ns and falls at
--   30 + 0.7 + 2 = 32.7 ns.
library ieee;
use ieee.std_logic_1164.all;

entity tb_path is end entity;

architecture t of tb_path is
  component vbuf port (a : in std_logic; y : out std_logic); end component;
  signal a : std_logic := '0';
  signal y : std_logic;
begin
  u1 : vbuf port map (a, y);

  stim : process
  begin
    wait for 10 ns;
    a <= '1';
    wait for 20 ns;
    a <= '0';
    wait;
  end process;

  mon : process (y)
  begin
    if y'event then
      report "TRACE y=" & std_logic'image(y) & " @" & time'image(now);
    end if;
  end process;
end architecture;

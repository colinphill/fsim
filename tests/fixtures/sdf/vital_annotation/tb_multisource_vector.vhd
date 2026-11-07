-- SPDX-License-Identifier: Apache-2.0
--
-- Two sources, u1 and u3, drive the bus m that is the input of the VITAL
-- bus buffer u2 (cells_vbus.vhd). multisource_vector_30.sdf gives each
-- source its INTERCONNECT delay to u2.a, the multiple source mode of IEEE
-- 1076.4-2000 5.2.7.1.5 for the vector generic tipd_a: it is entered for
-- an element when a second source reaches it, so elements 0 and 2 keep one
-- location for each source and the VitalWireDelay call of that element
-- (6.4.1) delays a change by the location of the source that changed it,
-- while elements 1 and 3 have the one source u1 and keep the plain
-- behaviour whichever source drives them.
--   (rise, fall)   element 0   element 1   element 2   element 3
--   u1             (4, 5)      (4, 5)      (4, 5)      (4, 5)
--   u3             (2, 3)      -           (6, 7)      -
-- The sources drive m one at a time, the other one drives 'Z'. y follows
-- the wire delayed bus without delay; its element 0 is the leftmost:
--   0 ns: a1 = "0000", a3 = "ZZZZ": m becomes "0000" with a change of both
--     sources (each from 'U'). Falls: element 0 min(5, 3) = 3 ns, element 2
--     min(5, 7) = 5 ns, elements 1 and 3 5 ns: y = "0UUU" at 3 ns and
--     "0000" at 5 ns.
--   10 ns: a1 rises, only u1 changes m: every element rises after 4 ns,
--     y = "1111" at 14 ns.
--   20 ns: a1 falls, only u1: y = "0000" at 25 ns.
--   25 ns: a1 = "ZZZZ" and a3 = "0000" in one delta: m stays "0000".
--   30 ns: a3 rises, only u3 changes m: element 0 after 2 ns (u3 rise),
--     elements 1 and 3 after 4 ns (u1 rise, the single source), element 2
--     after 6 ns: y = "1000" at 32 ns, "1101" at 34 ns, "1111" at 36 ns.
--   40 ns: a3 falls: element 0 after 3 ns, elements 1 and 3 after 5 ns,
--     element 2 after 7 ns: y = "0111" at 43 ns, "0010" at 45 ns, "0000" at
--     47 ns.
--   50 ns: a1 rises and a3 goes to 'Z' in one delta: both sources changed m,
--     and the minimum of their rise delays is taken: element 0 min(4, 2) =
--     2 ns, element 2 min(4, 6) = 4 ns, elements 1 and 3 4 ns: y = "1000" at
--     52 ns and "1111" at 54 ns.
--   60 ns: a1 = "ZZZZ" and a3 = "1111" in one delta: m stays "1111".
--   70 ns: a3 falls, only u3 changes m: y = "0111" at 73 ns, "0010" at
--     75 ns and "0000" at 77 ns.
library ieee;
use ieee.std_logic_1164.all;

entity vdrv4 is
  port (a : in std_logic_vector (0 to 3); y : out std_logic_vector (0 to 3));
end entity;

architecture t of vdrv4 is
begin
  y <= a;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity tb_multisource_vector is end entity;

architecture t of tb_multisource_vector is
  component vbus port (a : in std_logic_vector (0 to 3); y : out std_logic_vector (0 to 3)); end component;
  component vdrv4 port (a : in std_logic_vector (0 to 3); y : out std_logic_vector (0 to 3)); end component;
  signal a1 : std_logic_vector (0 to 3) := "0000";
  signal a3 : std_logic_vector (0 to 3) := "ZZZZ";
  signal m, y : std_logic_vector (0 to 3);
begin
  u1 : vdrv4 port map (a1, m);
  u3 : vdrv4 port map (a3, m);
  u2 : vbus port map (m, y);

  stim : process
  begin
    wait for 10 ns;
    a1 <= "1111";
    wait for 10 ns;
    a1 <= "0000";
    wait for 5 ns;
    a1 <= "ZZZZ";
    a3 <= "0000";
    wait for 5 ns;
    a3 <= "1111";
    wait for 10 ns;
    a3 <= "0000";
    wait for 10 ns;
    a1 <= "1111";
    a3 <= "ZZZZ";
    wait for 10 ns;
    a1 <= "ZZZZ";
    a3 <= "1111";
    wait for 10 ns;
    a3 <= "0000";
    wait;
  end process;

  mon_y : process (y)
  begin
    if y'event then
      report "TRACE y=" & to_string(y) & " @" & time'image(now);
    end if;
  end process;
end architecture;

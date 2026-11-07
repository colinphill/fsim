-- SPDX-License-Identifier: Apache-2.0
--
-- Two sources, u1 and u3, drive the net m that is the input of the VITAL
-- buffer u2. multisource_30.sdf gives each source its INTERCONNECT delay to
-- u2.a, the multiple source mode of IEEE 1076.4-2000 5.2.7.1.5: tipd_a of u2
-- keeps one location for each source, and the VitalWireDelay call of 6.4.1
-- delays a change of m by the location of the source that changed it.
-- u1 has (rise 1 ns, fall 2 ns), u3 has (rise 5 ns, fall 6 ns); u2 has the
-- default tpd_a_y = (1 ns, 2 ns). The sources drive m one at a time, the
-- other one drives 'Z':
--   0 ns: a1 = '0', a3 = 'Z'. m becomes '0' with a change of both sources
--     (each from 'U'); the selected delay is the minimum of the two falls,
--     2 ns: a_ipd falls at 2 ns and y falls at 2 + 2 = 4 ns.
--   10 ns: a1 rises, only u1 changes m: a_ipd rises at 11 ns, y at 12 ns.
--   20 ns: a1 falls, u1: a_ipd falls at 22 ns, y falls at 24 ns.
--   25 ns: a1 = 'Z' and a3 = '0' in one delta: m stays '0', no event.
--   30 ns: a3 rises, only u3 changes m: a_ipd rises at 35 ns, y at 36 ns.
--   40 ns: a3 falls, u3: a_ipd falls at 46 ns, y falls at 48 ns.
library ieee;
use ieee.std_logic_1164.all;

entity vdrv is
  port (a : in std_logic; y : out std_logic);
end entity;

architecture t of vdrv is
begin
  y <= a;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity tb_multisource is end entity;

architecture t of tb_multisource is
  component vbuf port (a : in std_logic; y : out std_logic); end component;
  component vdrv port (a : in std_logic; y : out std_logic); end component;
  signal a1 : std_logic := '0';
  signal a3 : std_logic := 'Z';
  signal m, y : std_logic;
begin
  u1 : vdrv port map (a1, m);
  u3 : vdrv port map (a3, m);
  u2 : vbuf port map (m, y);

  stim : process
  begin
    wait for 10 ns;
    a1 <= '1';
    wait for 10 ns;
    a1 <= '0';
    wait for 5 ns;
    a1 <= 'Z';
    a3 <= '0';
    wait for 5 ns;
    a3 <= '1';
    wait for 10 ns;
    a3 <= '0';
    wait;
  end process;

  mon_y : process (y)
  begin
    if y'event then
      report "TRACE y=" & std_logic'image(y) & " @" & time'image(now);
    end if;
  end process;
end architecture;

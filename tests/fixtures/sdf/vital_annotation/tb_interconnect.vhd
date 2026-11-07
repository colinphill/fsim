-- SPDX-License-Identifier: Apache-2.0
--
-- Two buffers in series. interconnect_30.sdf: INTERCONNECT u1.y u2.a (2, 3) ns
-- sets tipd_a of u2 (IEEE 1076.4-2000 5.2.7.1.4); u1 keeps the defaults
-- tipd_a = (0.5, 0.7) ns and tpd_a_y = (1, 2) ns, and so does tpd_a_y of u2.
--   u1: a_ipd falls at 0.7 ns, m falls at 0.7 + 2 = 2.7 ns; a rises at 10 ns,
--   m rises at 10.5 + 1 = 11.5 ns; a falls at 30 ns, m falls at 30.7 + 2 =
--   32.7 ns.
--   u2: its input follows m with its tipd_a: falling 3 ns, rising 2 ns, so
--   y falls at 2.7 + 3 + 2 = 7.7 ns, rises at 11.5 + 2 + 1 = 14.5 ns and falls
--   at 32.7 + 3 + 2 = 37.7 ns.
library ieee;
use ieee.std_logic_1164.all;

entity tb_interconnect is end entity;

architecture t of tb_interconnect is
  component vbuf port (a : in std_logic; y : out std_logic); end component;
  signal a : std_logic := '0';
  signal m, y : std_logic;
begin
  u1 : vbuf port map (a, m);
  u2 : vbuf port map (m, y);

  stim : process
  begin
    wait for 10 ns;
    a <= '1';
    wait for 20 ns;
    a <= '0';
    wait;
  end process;

  mon_m : process (m)
  begin
    if m'event then
      report "TRACE m=" & std_logic'image(m) & " @" & time'image(now);
    end if;
  end process;

  mon_y : process (y)
  begin
    if y'event then
      report "TRACE y=" & std_logic'image(y) & " @" & time'image(now);
    end if;
  end process;
end architecture;

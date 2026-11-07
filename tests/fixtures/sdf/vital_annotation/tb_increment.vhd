-- SPDX-License-Identifier: Apache-2.0
--
-- Two buffers with the stimulus of tb_path (a rises at 10 ns, falls at 30 ns).
-- increment_30.sdf: u1 gets ABSOLUTE tipd_a = (1, 2) ns and tpd_a_y = (3, 4) ns,
-- then INCREMENT (0.5, 0.5) and (1, 2) ns, so the generics are
-- tipd_a = (1.5, 2.5) ns and tpd_a_y = (4, 6) ns (IEEE 1076.4-2000 5.2.7.1.1).
--   y1 falls at 2.5 + 6 = 8.5 ns, rises at 10 + 1.5 + 4 = 15.5 ns and falls
--   at 30 + 2.5 + 6 = 38.5 ns.
-- u2 only gets INCREMENT tpd_a_y (1, 2) ns on its default (1, 2) ns: (2, 4) ns,
-- with the default tipd_a = (0.5, 0.7) ns.
--   y2 falls at 0.7 + 4 = 4.7 ns, rises at 10 + 0.5 + 2 = 12.5 ns and falls
--   at 30 + 0.7 + 4 = 34.7 ns.
library ieee;
use ieee.std_logic_1164.all;

entity tb_increment is end entity;

architecture t of tb_increment is
  component vbuf port (a : in std_logic; y : out std_logic); end component;
  signal a : std_logic := '0';
  signal y1, y2 : std_logic;
begin
  u1 : vbuf port map (a, y1);
  u2 : vbuf port map (a, y2);

  stim : process
  begin
    wait for 10 ns;
    a <= '1';
    wait for 20 ns;
    a <= '0';
    wait;
  end process;

  mon1 : process (y1)
  begin
    if y1'event then
      report "TRACE y1=" & std_logic'image(y1) & " @" & time'image(now);
    end if;
  end process;

  mon2 : process (y2)
  begin
    if y2'event then
      report "TRACE y2=" & std_logic'image(y2) & " @" & time'image(now);
    end if;
  end process;
end architecture;

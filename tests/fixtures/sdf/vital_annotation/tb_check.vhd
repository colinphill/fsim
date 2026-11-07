-- SPDX-License-Identifier: Apache-2.0
--
-- A flip-flop vff with check_30.sdf (or check_21.sdf): tpd_clk_q_posedge =
-- (2, 3) ns, tsetup_d_clk_noedge_posedge = 4 ns, thold_d_clk_noedge_posedge =
-- 2 ns, tpw_clk_posedge = 5 ns, tperiod_clk_posedge = 12 ns.
--   clk rises at 5 ns with d = 0: q falls (from 'U') 3 ns later, at 8 ns.
--   clk rises at 20 ns, d (1) changed 6 ns before: no setup violation; q rises
--   2 ns later, at 22 ns. The pulses so far are 7 ns wide, the period is 15 ns.
--   d falls at 21 ns, 1 ns after the edge: hold violation (hold 2 ns).
--   d rises at 31 ns and clk at 33 ns: 2 ns of setup, less than 4 ns, violation.
--   clk falls at 36 ns after a 3 ns pulse, less than 5 ns: pulse violation.
-- The model reports each violation after the VITAL message.
library ieee;
use ieee.std_logic_1164.all;

entity tb_check is end entity;

architecture t of tb_check is
  component vff port (d, clk : in std_logic; q : out std_logic); end component;
  signal d, clk : std_logic := '0';
  signal q : std_logic;
begin
  u : vff port map (d, clk, q);

  stim : process
  begin
    wait for 5 ns;
    clk <= '1';       -- 5: first rising edge
    wait for 7 ns;
    clk <= '0';       -- 12: pulse of 7 ns
    wait for 2 ns;
    d <= '1';         -- 14
    wait for 6 ns;
    clk <= '1';       -- 20: setup time 6 ns
    wait for 1 ns;
    d <= '0';         -- 21: hold time 1 ns
    wait for 6 ns;
    clk <= '0';       -- 27: pulse of 7 ns
    wait for 4 ns;
    d <= '1';         -- 31
    wait for 2 ns;
    clk <= '1';       -- 33: setup time 2 ns
    wait for 3 ns;
    clk <= '0';       -- 36: pulse of 3 ns
    wait;
  end process;

  mon : process (q)
  begin
    if q'event then
      report "TRACE q=" & std_logic'image(q) & " @" & time'image(now);
    end if;
  end process;
end architecture;

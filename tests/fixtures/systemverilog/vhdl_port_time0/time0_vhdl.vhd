-- SPDX-License-Identifier: Apache-2.0
-- The same leaf driven from VHDL: S'STABLE and S'QUIET in a sensitivity list
-- wake on the event and again on the return to TRUE (IEEE 1076-2019 16.2.4).
library ieee;
use ieee.std_logic_1164.all;
entity top is
end entity;
architecture sim of top is
  signal p : std_logic := '0';
begin
  u : entity work.stable_leaf port map (p => p);
  stim : process begin
    wait for 5 ns; p <= '1';
    wait for 7 ns; p <= '0';
    wait for 1 ns; std.env.finish;
  end process;
end architecture;

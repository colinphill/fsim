-- SPDX-License-Identifier: Apache-2.0
-- A VHDL parent of a SystemVerilog child: the shared std_logic starts at U,
-- which SystemVerilog reads as x; both sides sample between time steps.
library ieee;
use ieee.std_logic_1164.all;
entity top is
end entity;
architecture sim of top is
  signal p : std_logic;
  signal q : std_logic;
  component svleaf port (p : in std_logic; q : out std_logic); end component;
begin
  u : svleaf port map (p => p, q => q);
  stim : process begin
    wait for 700 ps;
    report "vhdl p=" & std_logic'image(p) & " q=" & std_logic'image(q);
    wait for 4300 ps; p <= '1';
    wait for 1 ns;
    report "vhdl p=" & std_logic'image(p) & " q=" & std_logic'image(q);
    std.env.finish;
  end process;
end architecture;

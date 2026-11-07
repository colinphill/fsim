-- SPDX-License-Identifier: Apache-2.0
-- A VHDL std_logic bus with a pull-up and a SystemVerilog inout child: the
-- bus resolves as the SystemVerilog-declared one in inout_net.sv.
library ieee;
use ieee.std_logic_1164.all;
entity top is
end entity;
architecture sim of top is
  component inout_svleaf is
    port (sda : inout std_logic);
  end component;
  signal sda : std_logic;
begin
  sda <= 'H';
  u : inout_svleaf port map (sda => sda);
  sample : process begin
    wait for 700 ps;
    for step in 0 to 3 loop
      report "vhdl " & integer'image(step) & " sda=" & std_logic'image(sda);
      wait for 1 ns;
    end loop;
    wait for 10 ns;
    std.env.finish;
  end process;
end architecture;

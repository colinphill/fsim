-- SPDX-License-Identifier: Apache-2.0
-- VHDL ports driving SystemVerilog nets: io and sda are inout std_logic on
-- SystemVerilog wires, o is an out port with default '1', d an out port
-- left at its default 'U'.
library ieee;
use ieee.std_logic_1164.all;
entity inout_leaf is
  port (io : inout std_logic; sda : inout std_logic;
        o : out std_logic := '1'; d : out std_logic);
end entity;
architecture rtl of inout_leaf is
begin
  drive : process begin
    wait for 1 ns; io <= 'H';
    wait for 1 ns; io <= 'L';
    wait for 1 ns; io <= 'W';
    wait for 1 ns; io <= '-';
    wait for 1 ns; io <= 'U';
    wait for 1 ns; io <= 'X';
    wait for 1 ns; io <= 'Z';
    wait;
  end process;
  -- A pull-up.
  sda <= 'H';
  drive_o : process begin
    wait for 3 ns; o <= '0';
    wait;
  end process;
  sample : process begin
    wait for 700 ps;
    for step in 0 to 7 loop
      report "vhdl " & integer'image(step) & " io=" & std_logic'image(io)
        & " sda=" & std_logic'image(sda);
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;

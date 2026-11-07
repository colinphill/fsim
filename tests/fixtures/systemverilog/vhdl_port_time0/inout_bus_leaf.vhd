-- SPDX-License-Identifier: Apache-2.0
-- A VHDL inout std_logic_vector on a 72-bit SystemVerilog net that
-- SystemVerilog also drives: whole and slice constants with 'H', 'L', 'W'
-- and '-', a constant at a computed index, and computed values; the VHDL
-- reader sees the resolved net through the logic mapping.
library ieee;
use ieee.std_logic_1164.all;
entity inout_bus_leaf is
  port (bus_io : inout std_logic_vector(71 downto 0));
end entity;
architecture rtl of inout_bus_leaf is
  signal data : std_logic_vector(7 downto 0) := x"a5";
begin
  drive : process
    variable index : integer := 7;
  begin
    bus_io <= (others => 'H');
    wait for 1 ns; bus_io(3 downto 0) <= "LHW-";
    for step in 0 to 3 loop
      wait for 1 ns;
      index := index + 1;
      bus_io(index) <= 'L';
    end loop;
    wait for 1 ns; bus_io(71 downto 64) <= data;
    wait for 1 ns; data <= not data; bus_io(63 downto 56) <= "UX01ZWLH";
    wait for 1 ns; bus_io(71 downto 64) <= data;
    wait for 1 ns; bus_io <= (others => 'Z');
    wait;
  end process;
  sample : process begin
    wait for 700 ps;
    for step in 0 to 10 loop
      report "vhdl " & integer'image(step) & " bus=" & to_string(bus_io);
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;

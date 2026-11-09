-- SPDX-License-Identifier: Apache-2.0
--
-- The VHDL side of wide_dyn_bus.sv: the process `dyn` assigns the N-bit signal
-- chunk to bus_io(idx + N - 1 downto idx), a slice at a nonstatic index of
-- the W-bit inout port, and `sample` prints the port at 0.6 ns + k ns.
library ieee;
use ieee.std_logic_1164.all;

entity wide_dyn_leaf is
  generic (W : positive := 72; N : positive := 65);
  port (bus_io : inout std_logic_vector(W - 1 downto 0));
end entity;

architecture rtl of wide_dyn_leaf is
  function pattern(p : natural) return std_logic is
  begin
    case p mod 8 is
      when 0 => return '1';
      when 1 => return '0';
      when 2 => return 'X';
      when 3 => return 'Z';
      when 4 => return 'L';
      when 5 => return 'H';
      when 6 => return 'W';
      when others => return '-';
    end case;
  end function;
  function make(shift : natural) return std_logic_vector is
    variable result : std_logic_vector(N - 1 downto 0);
  begin
    for q in result'range loop
      result(q) := pattern(q + shift);
    end loop;
    return result;
  end function;
  signal idx : natural := 3;
  signal chunk : std_logic_vector(N - 1 downto 0) := make(0);
begin
  stimulus : process begin
    wait for 1 ns;
    idx <= W - N;
    wait for 1 ns;
    idx <= 0;
    wait for 1 ns;
    chunk <= make(1);
    wait;
  end process;
  dyn : process (idx, chunk) begin
    bus_io(idx + N - 1 downto idx) <= chunk;
  end process;
  sample : process begin
    wait for 600 ps;
    for step in 0 to 4 loop
      report "inl vhdl " & integer'image(step) & " bus=" & to_string(bus_io);
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;

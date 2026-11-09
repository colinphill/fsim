-- SPDX-License-Identifier: Apache-2.0
--
-- The VHDL side of wide_bus.sv: a W-bit std_logic_vector inout port driven by
-- the process `drive` (a case on mode over the nine-value constants 'H', 'L'
-- and 'Z' and over the signal data, one of each std_logic value in turn),
-- sampled by `sample` at 0.6 ns + k ns.
library ieee;
use ieee.std_logic_1164.all;

entity wide_bus_leaf is
  generic (W : positive := 65);
  port (bus_io : inout std_logic_vector(W - 1 downto 0));
end entity;

architecture rtl of wide_bus_leaf is
  signal data : std_logic_vector(W - 1 downto 0) := (others => '0');
  signal mode : natural := 0;
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
begin
  fill : process begin
    for p in 0 to W - 1 loop
      data(p) <= pattern(p);
    end loop;
    for m in 1 to 4 loop
      wait for 1 ns;
      mode <= m mod 4;
    end loop;
    wait;
  end process;
  drive : process (mode, data) begin
    case mode is
      when 0 => bus_io <= (others => 'H');
      when 1 => bus_io <= data;
      when 2 => bus_io <= (others => 'L');
      when others => bus_io <= (others => 'Z');
    end case;
  end process;
  sample : process begin
    wait for 600 ps;
    for step in 0 to 5 loop
      report "inl vhdl " & integer'image(step) & " bus=" & to_string(bus_io);
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;

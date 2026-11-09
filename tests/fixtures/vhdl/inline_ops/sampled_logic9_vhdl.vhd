-- SPDX-License-Identifier: Apache-2.0
--
-- The VHDL side of sampled_logic9.sv: a top level that drives a std_logic sb
-- and an 80 element std_logic_vector sv into the SystemVerilog module svm,
-- one table row every 10 ns from 10 ns on (so a row is applied between the
-- posedges of the module's clock, which has its edges at 10 n + 5). Both are
-- 'U' until the first row. The patterns of sv are all '0', all '1', "Z" x16
-- then '0' x64, "X" x16 then '1' x64, all 'H' and all 'L'.
library ieee;
use ieee.std_logic_1164.all;

entity vtop is
end entity;

architecture a of vtop is
  component svm
    port (sb : in std_logic; sv : in std_logic_vector(79 downto 0));
  end component;
  signal sb : std_logic;
  signal sv : std_logic_vector(79 downto 0);
  constant p0 : std_logic_vector(79 downto 0) := (others => '0');
  constant p1 : std_logic_vector(79 downto 0) := (others => '1');
  constant p2 : std_logic_vector(79 downto 0) := (79 downto 64 => 'Z', others => '0');
  constant p3 : std_logic_vector(79 downto 0) := (79 downto 64 => 'X', others => '1');
  constant ph : std_logic_vector(79 downto 0) := (others => 'H');
  constant pl : std_logic_vector(79 downto 0) := (others => 'L');
  type pattern_table is array (0 to 39) of integer range 0 to 5;
  constant pattern : pattern_table := (3, 2, 5, 5, 4, 0, 4, 2, 4, 0, 4, 4, 0, 2, 3, 1, 3, 0, 3, 2, 1, 5, 4, 0, 4, 3, 3, 4, 0, 3, 2, 1, 2, 4, 2, 0, 0, 2, 0, 0);
  constant bits : string(1 to 40) := "LX00L0LXXXH0Z110LHZ1ZZ0L1ZLZL1X0H01H0LXH";
begin
  u : svm port map (sb => sb, sv => sv);
  drive : process
  begin
    for k in pattern'range loop
      wait for 10 ns;
      case pattern(k) is
        when 0 => sv <= p0;
        when 1 => sv <= p1;
        when 2 => sv <= p2;
        when 3 => sv <= p3;
        when 4 => sv <= ph;
        when others => sv <= pl;
      end case;
      case bits(k + 1) is
        when '0' => sb <= '0';
        when '1' => sb <= '1';
        when 'H' => sb <= 'H';
        when 'L' => sb <= 'L';
        when 'Z' => sb <= 'Z';
        when others => sb <= 'X';
      end case;
    end loop;
    wait;
  end process;
end architecture;

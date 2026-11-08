-- SPDX-License-Identifier: Apache-2.0
-- Compiled as VHDL-2002 over the VHDL-1993 leaf. n = 10 from the leaf;
-- m = to_integer(unsigned("1010") + 1) = 11 in this unit.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity top is
end entity;

architecture a of top is
  signal s : std_logic_vector(3 downto 0) := "1010";
  signal n : integer := 0;
  signal m : integer := 0;
begin
  u : entity work.leaf port map (s => s, n => n);
  m <= to_integer(unsigned(s) + 1);

  process begin
    wait for 5 ns;
    wait;
  end process;
end architecture;

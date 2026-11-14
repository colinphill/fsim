-- SPDX-License-Identifier: Apache-2.0
-- A VHDL top level over a SystemVerilog leaf. The assignments of the
-- initialization phase update their signals in the first simulation cycle, so
-- the callbacks of the initialization phase and vhpiCbStartOfSimulation see
-- the initial values (IEEE 1076-2019 14.7.5.2, 14.7.5.3, 21.3.7.5, 21.3.7.6)
-- whether the design is VHDL only or mixed with SystemVerilog.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  component leaf is
    port (a : in std_logic; y : out std_logic; q : out std_logic_vector(3 downto 0));
  end component;
  signal s : std_logic_vector(3 downto 0) := "0000";
  signal x, z : std_logic := '0';
  signal q : std_logic_vector(3 downto 0);
begin
  s <= "1010";
  x <= '1';
  u : leaf port map (a => x, y => z, q => q);
end architecture;

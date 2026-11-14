-- SPDX-License-Identifier: Apache-2.0
-- The VHDL register stage of foreign_vpi_mixed_clock.sv: a process
-- sensitive to the clock and an asynchronous reset (IEEE 1076-2019 11.3).
library ieee;
use ieee.std_logic_1164.all;

entity mixed_clock_stage is
  port (clk, reset_n, valid : in std_logic;
        d : in std_logic_vector(7 downto 0);
        q : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of mixed_clock_stage is
begin
  process (clk, reset_n)
  begin
    if reset_n = '0' then
      q <= (others => '0');
    elsif rising_edge(clk) then
      if valid = '1' then
        q <= d;
      end if;
    end if;
  end process;
end architecture;

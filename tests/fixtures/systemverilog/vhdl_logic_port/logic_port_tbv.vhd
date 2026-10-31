-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

-- The reference: the same stimulus from a VHDL driver.
entity logic_port_tbv is
end entity;

architecture sim of logic_port_tbv is
    signal clk : std_logic := 'X';
    signal v : std_logic_vector(3 downto 0) := "ZZ10";
begin
    dut : entity work.logic_port_dut port map (clk => clk, v => v);
    process
    begin
        wait for 1 ns; clk <= '0'; v <= "1111";
        wait for 1 ns; clk <= '1';
        wait for 1 ns; clk <= 'Z'; v <= "X001";
        wait;
    end process;
end architecture;

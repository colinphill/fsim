-- SPDX-License-Identifier: Apache-2.0
-- Kernel image witness (VHDL): identical lane instances (specialized
-- members), a function, port maps with a type conversion on an actual and
-- slice associations (conversions and wired formals), and a reporting
-- process.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity kernel_image_vhdl_lane is
    generic (seed : natural := 1);
    port (
        clk : in std_logic;
        rst : in std_logic;
        step : in unsigned(3 downto 0);
        value : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of kernel_image_vhdl_lane is
    function scramble(x : unsigned(7 downto 0); s : natural) return unsigned is
    begin
        return (x(6 downto 0) & (x(7) xor x(5))) + to_unsigned(s mod 256, 8);
    end function;
    signal state : unsigned(7 downto 0);
    signal mixed : unsigned(7 downto 0);
begin
    mixed <= scramble(state, seed) xor resize(step, 8);

    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                state <= to_unsigned(seed mod 256, 8);
            else
                state <= mixed;
            end if;
        end if;
    end process;

    value <= std_logic_vector(mixed);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity kernel_image_vhdl is
end entity;

architecture tb of kernel_image_vhdl is
    type lane_values is array (0 to 3) of std_logic_vector(7 downto 0);
    signal clk : std_logic := '0';
    signal rst : std_logic := '1';
    signal counter : std_logic_vector(7 downto 0) := (others => '0');
    signal lanes : lane_values;
    signal done : boolean := false;
begin
    clk <= not clk after 5 ns when not done else clk;

    gen : for lane in 0 to 3 generate
        unit : entity work.kernel_image_vhdl_lane
            generic map (seed => lane * 37 + 5)
            port map (
                clk => clk,
                rst => rst,
                step => unsigned(counter(lane + 3 downto lane)),
                value => lanes(lane));
    end generate;

    process (clk)
    begin
        if rising_edge(clk) then
            counter <= std_logic_vector(unsigned(counter) + 1);
        end if;
    end process;

    process
        variable text : line;
    begin
        wait until rising_edge(clk);
        wait until rising_edge(clk);
        rst <= '0';
        for cycle in 0 to 23 loop
            wait until falling_edge(clk);
            write(text, string'("lanes="));
            for lane in 0 to 3 loop
                write(text, to_hstring(lanes(lane)));
                write(text, string'(" "));
            end loop;
            write(text, string'("counter="));
            write(text, to_hstring(counter));
            writeline(output, text);
        end loop;
        done <= true;
        wait;
    end process;
end architecture;

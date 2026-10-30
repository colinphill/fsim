-- SPDX-License-Identifier: Apache-2.0
-- Clock domain witnesses (Phase 7 P1), driven by SystemVerilog
-- testbenches: a divider chain with an asynchronous-reset counter, a
-- latch-based clock gate, a glitchy AND gate, strict rising edges, and a
-- buffered clock.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Divider chain: div2 toggles on clk, div4 on div2; a counter on each.
entity cd_divider is
    port (
        clk : in std_logic;
        rst : in std_logic;
        q2 : out std_logic_vector(3 downto 0);
        q4 : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of cd_divider is
    signal div2 : std_logic := '0';
    signal div4 : std_logic := '0';
    signal c2 : unsigned(3 downto 0) := (others => '0');
    signal c4 : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            div2 <= not div2;
        end if;
    end process;
    process (div2)
    begin
        if rising_edge(div2) then
            div4 <= not div4;
            c2 <= c2 + 1;
        end if;
    end process;
    process (div4, rst)
    begin
        if rst = '1' then
            c4 <= (others => '0');
        elsif rising_edge(div4) then
            c4 <= c4 + 1;
        end if;
    end process;
    q2 <= std_logic_vector(c2);
    q4 <= std_logic_vector(c4);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Latch-based clock gate: the enable latch is transparent while clk is
-- low, so gclk = clk and en_l cannot glitch at the rising edge.
entity cd_icg is
    port (
        clk : in std_logic;
        en : in std_logic;
        q : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of cd_icg is
    signal en_l : std_logic := '0';
    signal gclk : std_logic;
    signal c : unsigned(3 downto 0) := (others => '0');
begin
    process (clk, en)
    begin
        if clk = '0' then
            en_l <= en;
        end if;
    end process;
    gclk <= clk and en_l;
    process (gclk)
    begin
        if rising_edge(gclk) then
            c <= c + 1;
        end if;
    end process;
    q <= std_logic_vector(c);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Glitchy gate: the enable is a flop on the same clock edge, so gclk can
-- pulse in the deltas after the edge. A cycle schedule must fall back.
entity cd_glitch is
    port (
        clk : in std_logic;
        en_d : in std_logic;
        q : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of cd_glitch is
    signal en : std_logic := '0';
    signal gclk : std_logic;
    signal c : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            en <= en_d;
        end if;
    end process;
    gclk <= clk and en;
    process (gclk)
    begin
        if rising_edge(gclk) then
            c <= c + 1;
        end if;
    end process;
    q <= std_logic_vector(c);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Edge predicates on metavalue transitions: the clock goes 'U' to '1', '0'
-- to 'X' and 'X' to '1', none a rising edge, then 'L' to 'H' and '0' to
-- '1', both rising edges (IEEE 1076-2008 16.7). The counter must end at 2.
entity cd_edge is
    port (count : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of cd_edge is
    signal clk : std_logic;
    signal c : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            c <= c + 1;
        end if;
    end process;
    process
    begin
        wait for 1 ns; clk <= '1';
        wait for 1 ns; clk <= '0';
        wait for 1 ns; clk <= 'X';
        wait for 1 ns; clk <= '1';
        wait for 1 ns; clk <= 'L';
        wait for 1 ns; clk <= 'H';
        wait for 1 ns; clk <= '0';
        wait for 1 ns; clk <= '1';
        wait;
    end process;
    count <= std_logic_vector(c);
end architecture;

-- Buffered clock: the flop's clock is a conversion of the testbench clock
-- (a primary clock through combinational logic, not a gate).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity cd_buffer is
    port (
        clk : in std_logic;
        count : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of cd_buffer is
    signal clk_b : bit;
    signal c : unsigned(3 downto 0) := (others => '0');
begin
    clk_b <= '1' when clk = '1' else '0';
    process (clk_b)
    begin
        if clk_b'event and clk_b = '1' then
            c <= c + 1;
        end if;
    end process;
    count <= std_logic_vector(c);
end architecture;

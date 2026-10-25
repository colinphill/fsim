-- fsim: top=vhdl_generate_composite_constant_wide_resolved_20261024(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- A composite constant of a generate body whose initializer is a function
-- call elaborates for every instance (IEEE 1076-2019 14.4.2.1): after an
-- earlier instance associated a type conversion with a port, and when the
-- array is wider than the integral and packed-array folding paths represent.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_generate_composite_constant_rom_20261024 is
    generic (N : integer; M : integer; ENABLE : integer);
    port (idx : in std_logic_vector(10 downto 0);
          q : out std_logic_vector(M - 1 downto 0));
end entity;

architecture rtl of vhdl_generate_composite_constant_rom_20261024 is
begin
    g_rom : if ENABLE /= 0 generate
        type rom_t is array (0 to N) of std_logic_vector(M - 1 downto 0);
        function build return rom_t is
            variable r : rom_t;
        begin
            for e in 0 to N loop
                r(e) := std_logic_vector(to_unsigned(e + 1, M));
            end loop;
            return r;
        end function;
        constant rom : rom_t := build;
    begin
        q <= rom(to_integer(unsigned(idx)));
    end generate;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_generate_composite_constant_wide_resolved_20261024 is
end entity;

architecture sim of vhdl_generate_composite_constant_wide_resolved_20261024 is
    signal i3, i5, i11 : unsigned(10 downto 0) := (others => '0');
    signal q3 : std_logic_vector(2 downto 0);
    signal q5 : std_logic_vector(4 downto 0);
    signal q11 : std_logic_vector(10 downto 0);
begin
    u3 : entity work.vhdl_generate_composite_constant_rom_20261024
        generic map (N => 6, M => 3, ENABLE => 1) port map (std_logic_vector(i3), q3);
    u5 : entity work.vhdl_generate_composite_constant_rom_20261024
        generic map (N => 12, M => 5, ENABLE => 1) port map (std_logic_vector(i5), q5);
    u11 : entity work.vhdl_generate_composite_constant_rom_20261024
        generic map (N => 2047, M => 11, ENABLE => 1) port map (std_logic_vector(i11), q11);

    check : process
    begin
        i3 <= to_unsigned(4, 11);
        i5 <= to_unsigned(9, 11);
        i11 <= to_unsigned(2000, 11);
        wait for 1 ns;
        assert q3 = "101" report "q3" severity failure;
        assert q5 = "01010" report "q5" severity failure;
        assert q11 = "11111010001" report "q11" severity failure;
        report "PASS";
        wait;
    end process;
end architecture;

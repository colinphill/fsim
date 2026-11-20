-- SPDX-License-Identifier: Apache-2.0
-- kernel_coverage.vhd with the next value of d resized to d's 8 bits:
-- NUMERIC_STD "*" (UNSIGNED, NATURAL) returns 2 * L'LENGTH bits (IEEE
-- 1076-2019 16.8.5), so kernel_coverage.vhd's assignment is a length error
-- (owner 11-19: that fixture stays as the negative case).
-- Code coverage counted by the v4 kernel's own instructions in VHDL: hits
-- of an entity instantiated twice, and toggles of std_logic values where
-- 'L' and 'H' are 0 and 1 and 'U', 'W' and 'Z' are unknown (a transition
-- from or to one counts in neither direction), narrow and 100 bits wide.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity kc_vleaf is
  port (clk : in std_logic; d : in std_logic_vector(7 downto 0);
        q : out std_logic_vector(7 downto 0));
end entity;
architecture rtl of kc_vleaf is
  signal r : std_logic_vector(7 downto 0);
begin
  process (clk) begin
    if rising_edge(clk) then
      if d(0) = '1' then
        r <= std_logic_vector(unsigned(d) + 1);
      else
        r <= d xor x"5a";
      end if;
      q <= r;
    end if;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity kernel_coverage_vhdl is
end entity;
architecture sim of kernel_coverage_vhdl is
  signal clk : std_logic := '0';
  signal d : std_logic_vector(7 downto 0) := (others => '0');
  signal q0, q1 : std_logic_vector(7 downto 0);
  signal wide : std_logic_vector(99 downto 0) := (others => 'U');
  signal weak : std_logic_vector(3 downto 0) := "LHWZ";
begin
  u0 : entity work.kc_vleaf port map (clk, d, q0);
  u1 : entity work.kc_vleaf port map (clk, not d, q1);
  clk <= not clk after 5 ns when now < 2000 ns else clk;
  process (clk) begin
    if rising_edge(clk) then
      d <= std_logic_vector(resize(unsigned(d) * 13 + 7, 8));
      wide <= wide(98 downto 0) & d(3);
      weak <= weak(2 downto 0) & weak(3);
    end if;
  end process;
end architecture;
